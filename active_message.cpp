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

// $ ./openshmem-am-root/bin/oshc++ -std=c++14 active_message.cpp 
// $ ./openshmem-am-root/bin/oshrun ./a.out -n 2
// Hello, world from PE 1 with value 7!
// Hello, world from PE 0 with value 13!

#include <shmemx.h>
#include <iostream>
#include <cassert>
#include <thread>
#include <cstring>
#include <vector>
#include <sstream>
#include <utility>
#include <type_traits>

#define __REQUIRES(...) typename std::enable_if<(__VA_ARGS__)>::type* = nullptr


// this serialization scheme is based on Cereal
// see http://uscilab.github.io/cereal

template<class OutputArchive, class T>
void serialize(OutputArchive& ar, const T& value)
{
  ar.stream() << value;
}

template<class OutputArchive, class Result, class... Args>
void serialize(OutputArchive& ar, Result (*const &fun_ptr)(Args...))
{
  void* void_ptr = reinterpret_cast<void*>(fun_ptr);

  serialize(ar, void_ptr);
}

template<class InputArchive, class T>
void deserialize(InputArchive& ar, T& value)
{
  ar.stream() >> value;
}

template<class InputArchive, class Result, class... Args>
void deserialize(InputArchive& ar, Result (*&fun_ptr)(Args...))
{
  void* void_ptr = nullptr;
  deserialize(ar, void_ptr);

  using function_ptr_type = Result (*)(Args...);
  fun_ptr = reinterpret_cast<function_ptr_type>(void_ptr);
}

template<size_t Index, class InputArchive, class... Ts, __REQUIRES(Index == sizeof...(Ts))>
void deserialize_tuple_impl(InputArchive& ar, std::tuple<Ts...>& tuple)
{
}

template<size_t Index, class InputArchive, class... Ts, __REQUIRES(Index < sizeof...(Ts))>
void deserialize_tuple_impl(InputArchive& ar, std::tuple<Ts...>& tuple)
{
  deserialize(ar, std::get<Index>(tuple));
  deserialize_tuple_impl<Index+1>(ar, tuple);
}

template<class InputArchive, class... Ts>
void deserialize(InputArchive& ar, std::tuple<Ts...>& tuple)
{
  deserialize_tuple_impl<0>(ar, tuple);
}

class output_archive
{
  public:
    inline output_archive(std::ostream& os)
      : stream_(os)
    {}

    inline ~output_archive()
    {
      stream_.flush();
    }

    template<class Arg, class... Args>
    void operator()(const Arg& arg, const Args&... args)
    {
      serialize(*this, arg);

      // insert a separator after each element
      stream_ << " ";

      (*this)(args...);
    }

    inline std::ostream& stream()
    {
      return stream_;
    }

  private:
    // this is the terminal case of operator() above
    // it never needs to be called by a client
    inline void operator()() {}

    std::ostream& stream_;
};

class input_archive
{
  public:
    inline input_archive(std::istream& is)
      : stream_(is)
    {}

    template<class Arg, class... Args>
    void operator()(Arg& arg, Args&... args)
    {
      deserialize(*this, arg);

      (*this)(args...);
    }

    inline std::istream& stream()
    {
      return stream_;
    }

  private:
    // this is the terminal case of operator() above
    // it never needs to be called by a client
    inline void operator()() {}

    std::istream& stream_;
};

template<size_t... Indices, class T, class... Ts>
std::tuple<Ts...> tail_impl(std::index_sequence<Indices...>, const std::tuple<T,Ts...>& t)
{
  return std::tuple<Ts...>(std::get<1 + Indices>(t)...);
}

template<class T, class... Ts>
std::tuple<Ts...> tail(const std::tuple<T,Ts...>& t)
{
  return tail_impl(std::make_index_sequence<sizeof...(Ts)>(), t);
}

template<size_t... Indices, class Function, class Tuple>
auto apply_impl(std::index_sequence<Indices...>, Function&& f, Tuple&& t)
{
  return std::forward<Function>(f)(std::get<Indices>(std::forward<Tuple>(t))...);
}

template<class Function, class Tuple>
auto apply(Function&& f, Tuple&& t)
{
  static constexpr size_t num_args = std::tuple_size<std::decay_t<Tuple>>::value;
  return apply_impl(std::make_index_sequence<num_args>(), std::forward<Function>(f), std::forward<Tuple>(t));
}

struct active_message
{
  public:
    template<class FunctionPtr, class... Args>
    explicit active_message(FunctionPtr func, Args... args)
      : message_(pack(&unpack_and_invoke<FunctionPtr,Args...>, func, args...))
    {}

    active_message(char* message, size_t num_bytes)
      : message_(message, num_bytes)
    {}

    size_t size() const
    {
      return message_.size();
    }

    const char* data() const
    {
      return message_.data();
    }

    void activate() const
    {
      std::stringstream is(message_);

      // extract a function_ptr_type from the beginning of the buffer
      input_archive archive(is);
      using function_ptr_type = void (*)(input_archive&);
      function_ptr_type invoke_me = nullptr;
      archive(invoke_me);

      // invoke the function pointer on the remaining data
      invoke_me(archive);
    }

  private:
    template<class FunctionPtr, class... Args>
    static void unpack_and_invoke(input_archive& archive)
    {
      // unpack function pointer and its arguments
      std::tuple<FunctionPtr,Args...> function_and_args;
      archive(function_and_args);

      // split tuple into a funciton pointer and arguments
      FunctionPtr f = std::get<0>(function_and_args);
      std::tuple<Args...> arguments = tail(function_and_args);

      apply(f, arguments);
    }

    template<class... Args>
    static std::string pack(const Args&... args)
    {
      std::stringstream os;

      {
        output_archive archive(os);

        // pack arguments into the archive
        archive(args...);
      }

      return os.str();
    }

    std::string message_;
};

void active_message_handler(void* data_buffer, size_t buffer_size, int calling_pe, shmemx_am_token_t token)
{
  active_message message(reinterpret_cast<char*>(data_buffer), buffer_size);

  message.activate();
}

void hello_world(int value)
{
  std::cout << "Hello, world from PE " << shmem_my_pe() << " with value " << value << "!" << std::endl;
}

int main()
{
  shmem_init();

  shmemx_am_attach(0, active_message_handler);

  if(shmem_my_pe() == 0)
  {
    active_message message(hello_world, 7);
    shmemx_am_request(1, 0, const_cast<char*>(message.data()), message.size());
  }
  else
  {
    active_message message(hello_world, 13);
    shmemx_am_request(0, 0, const_cast<char*>(message.data()), message.size());
  }

  shmemx_am_quiet();

  shmem_barrier_all();
}

