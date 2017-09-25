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
#include "execution_context.hpp"

class remote_executor
{
  public:
    inline explicit remote_executor(int node)
      : node_(node)
    {}

    inline remote_executor()
      : remote_executor(0)
    {}

    inline execution_context& context() const
    {
      return system_context();
    }

    template<class Function,
             __REQUIRES(can_serialize<typename std::decay<Function>::type>::value),
             __REQUIRES(can_deserialize<typename std::decay<Function>::type>::value),
             __REQUIRES(is_invocable<typename std::decay<Function>::type>::value)
            >
    std::future<invoke_result_t<typename std::decay<Function>::type>>
      twoway_execute(Function&& f) const
    {
      return context().two_sided_execute(node(), std::forward<Function>(f));
    }

    inline int node() const
    {
      return node_;
    }

  private:
    int node_;
};
