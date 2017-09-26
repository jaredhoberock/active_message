// Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <shmemx.h>
#include <unordered_map>
#include <utility>
#include <tuple>
#include <string>
#include <type_traits>
#include <thread>
#include <atomic>

#include "active_message.hpp"

class execution_context
{
  public:
    inline execution_context()
      : continue_polling_{true}
    {
      // start shmem
      shmem_init();

      // register handlers
      shmemx_am_attach(one_sided_request_handler_id_, one_sided_request_handler);
      shmemx_am_attach(two_sided_request_handler_id_, two_sided_request_handler);
      shmemx_am_attach(two_sided_reply_handler_id_,   two_sided_reply_handler);

      // begin polling
      polling_thread_ = std::thread([this]
      {
        while(continue_polling_)
        {
          shmemx_am_poll();
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
      });
    }

    inline ~execution_context()
    {
      continue_polling_ = false;

      polling_thread_.join();

      // XXX note that we don't call shmem_finalize() because it may already have been shutdown
    }

    inline std::size_t node_count() const
    {
      return shmem_n_pes();
    }
    
    inline void wait_for_all()
    {
      shmemx_am_quiet();
    }

    template<class Function, class... Args,
             __REQUIRES(can_serialize_all<Function,Args...>::value),
             __REQUIRES(can_deserialize_all<Function,Args...>::value),
             __REQUIRES(is_invocable<typename std::decay<Function>::type,typename std::decay<Args>::type...>::value)
            >
    void one_sided_execute(std::size_t node, Function&& f, Args&&... args)
    {
      // create a message
      active_message message(decay_copy(std::forward<Function>(f)), decay_copy(std::forward<Args>(args))...);

      // serialize the message
      std::string serialized_message = to_string(message);

      // transmit the serialization
      shmemx_am_request(node, one_sided_request_handler_id_, const_cast<char*>(serialized_message.data()), serialized_message.size());
    }

    template<class Function, class... Args,
             __REQUIRES(can_serialize_all<Function,Args...>::value),
             __REQUIRES(can_deserialize_all<Function,Args...>::value),
             __REQUIRES(is_invocable<typename std::decay<Function>::type,typename std::decay<Args>::type...>::value)
            >
    std::future<invoke_result_t<Function,typename std::decay<Args>::type...>>
      two_sided_execute(std::size_t node, Function&& f, Args&&... args)
    {
      using result_type = invoke_result_t<Function,typename std::decay<Args>::type...>;

      // create a new unfulfilled promise
      std::pair<int, std::future<result_type>> id_and_future = unfulfilled_promises<result_type>().add();

      // create a message
      two_sided_active_message message(decay_copy(std::forward<Function>(f)), std::make_tuple(decay_copy(std::forward<Args>(args))...), &fulfill_promise<result_type>, std::make_tuple(id_and_future.first));

      // serialize the message
      std::string serialized_message = to_string(message);

      // transmit the serialization
      shmemx_am_request(node, two_sided_request_handler_id_, const_cast<char*>(serialized_message.data()), serialized_message.size());

      // return the future
      return std::move(id_and_future.second);
    }

  private:
    const static int one_sided_request_handler_id_ = 0;
    const static int two_sided_request_handler_id_ = 1;
    const static int two_sided_reply_handler_id_   = 2;

    inline static void one_sided_request_handler(void* data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
    {
      // deserialize the message
      const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
      active_message message = from_string<active_message>(data_buffer, buffer_size);

      // activate the message and discard the result
      message.activate();
    }

    inline static void two_sided_request_handler(void* data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
    {
      // deserialize the message
      const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
      two_sided_active_message message = from_string<two_sided_active_message>(data_buffer, buffer_size);

      // activate the message and get the reply
      active_message reply = message.activate();

      // serialize the reply
      std::string serialized_reply = to_string(reply);

      // transmit the serialization
      shmemx_am_reply(two_sided_reply_handler_id_, const_cast<char*>(serialized_reply.data()), serialized_reply.size(), token);
    }

    inline static void two_sided_reply_handler(void *data_buffer_, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
    {
      // deserialize the reply
      const char* data_buffer = reinterpret_cast<const char*>(const_cast<const void*>(data_buffer_));
      active_message reply = from_string<active_message>(data_buffer, buffer_size);

      // activate the reply
      reply.activate();
    }

    template<class T>
    class promise_collection
    {
      public:
        promise_collection()
          : counter_{}
        {}
    
        std::pair<int, std::future<T>> add()
        {
          std::lock_guard<std::mutex> lock(mutex_);

          int id = make_id();
    
          std::promise<T> promise;
    
          std::future<T> future = promise.get_future();
    
          promises_.emplace(id, std::move(promise));
    
          return std::make_pair(id, std::move(future));
        }
    
        template<class U>
        void fulfill(int which, U&& result)
        {
          std::lock_guard<std::mutex> lock(mutex_);

          // move the promise out of the collection
          std::promise<T> promise = std::move(promises_[which]);
    
          // erase that position from the collection
          promises_.erase(which);
    
          // set the promise's value
          promise.set_value(std::forward<U>(result));
        }
    
      private:
        int make_id()
        {
          return counter_++;
        }
    
        std::mutex mutex_;
        int counter_;
        std::unordered_map<int, std::promise<T>> promises_;
    };

    template<class T>
    static promise_collection<T>& unfulfilled_promises()
    {
      static promise_collection<T> result;
      return result;
    }

    template<class T>
    static void fulfill_promise(T result, int which)
    {
      unfulfilled_promises<T>().fulfill(which, std::move(result));
    }

    template<class Arg>
    static typename std::decay<Arg>::type decay_copy(Arg&& arg)
    {
      return std::forward<Arg>(arg);
    }

    // this flag lets the polling thread to know when to stop polling
    std::atomic<bool> continue_polling_;

    // this thread calls shmemx_am_poll, which allows other threads on this node to make progress
    std::thread polling_thread_;
};


// this needs to be declared outside of system_context() so that
// its constructor is called collectively by all nodes
execution_context system_context_;

inline execution_context& system_context()
{
  return system_context_;
}

