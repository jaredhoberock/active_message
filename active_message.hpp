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

#include "serialization.hpp"

class active_message
{
  public:
    active_message() = default;

    template<class FunctionPtr, class... Args>
    explicit active_message(FunctionPtr func, Args... args)
      : message_(func, args...)
    {}

    any activate() const
    {
      return message_();
    }

    template<class OutputArchive>
    friend void serialize(OutputArchive& ar, const active_message& self)
    {
      ar(self.message_);
    }

    template<class InputArchive>
    friend void deserialize(InputArchive& ar, active_message& self)
    {
      ar(self.message_);
    }

  private:
    serializable_closure message_;
};


class two_sided_active_message : private active_message
{
  private:
    using super_t = active_message;

    // two_sided_active_message's constructor initializes the base active_message class with this function
    // as the function to call. The user's functions and arguments passed to two_sided_active_message's constructor are this function's arguments.
    // the result of this function is an active_message containing the reply
    template<class FunctionPtr1, class FunctionPtr2, class... Args>
    static active_message invoke_and_return_active_message_reply(FunctionPtr1 reply_func, FunctionPtr2 func, Args... args)
    {
      // invoke the user's function
      auto user_result = func(args...);

      // make an active_message containing the reply
      return active_message(reply_func, user_result);
    }

  public:
    two_sided_active_message() = default;

    template<class FunctionPtr1, class FunctionPtr2, class... Args>
    two_sided_active_message(FunctionPtr1 reply_func, FunctionPtr2 func, Args... args)
      : super_t(&invoke_and_return_active_message_reply<FunctionPtr1,FunctionPtr2,Args...>, reply_func, func, args...)
    {}

    active_message activate() const
    {
      return any_cast<active_message>(super_t::activate());
    }

    template<class OutputArchive>
    friend void serialize(OutputArchive& ar, const two_sided_active_message& self)
    {
      ar(static_cast<const active_message&>(self));
    }

    template<class InputArchive>
    friend void deserialize(InputArchive& ar, two_sided_active_message& self)
    {
      ar(static_cast<active_message&>(self));
    }
};

