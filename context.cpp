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

// $ ./openshmem-am-root/bin/oshc++ -std=c++11 context.cpp 
// $ ./openshmem-am-root/bin/oshrun ./a.out -n 2
// PE 0: Waiting on future
// PE 1: Waiting for all previously submitted work to complete
// PE 1: Hello, world with value 7!
// PE 0: Hello, world with value 13!
// PE 0: Future satisfied with result: 13
// PE 1: All previously submitted work complete

#include <iostream>
#include <future>
#include <cassert>

#include "execution_context.hpp"


int hello_world(int value)
{
  std::cout << "PE " << shmem_my_pe() << ": Hello, world with value " << value << "!" << std::endl;

  return 13;
}

int main()
{
  if(shmem_my_pe() == 0)
  {
    std::future<int> future = system_context().two_sided_execute(1, hello_world, 7);

    std::cout << "PE 0: Waiting on future" << std::endl;
    int result = future.get();
    assert(result == 13);

    std::cout << "PE 0: Future satisfied with result: " << result << std::endl;
  }
  else
  {
    system_context().one_sided_execute(0, hello_world, 13);

    std::cout << "PE 1: Waiting for all previously submitted work to complete" << std::endl;
    system_context().wait_for_all();

    std::cout << "PE 1: All previously submitted work complete" << std::endl;
  }
}

