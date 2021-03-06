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

// $ ./openshmem-am-root/bin/oshc++ -std=c++11 executor.cpp 
// $ ./openshmem-am-root/bin/oshrun ./a.out -n 2
// PE 0: Waiting on future
// PE 1: Hello, world!
// PE 0: Future satisfied with result 13

// oshc++-free build command:
// g++ -std=c++14 -Iopenshmem-am-root/include executor.cpp -Lopenshmem-am-root/lib -lopenshmem -Lgasnet-root/lib -lgasnet-smp-par -lpthread -lrt -lelf
// $ ./openshmem-am-root/bin/oshrun ./a.out -n 2
// PE 0: Waiting on future
// PE 1: Hello, world!
// PE 0: Future satisfied with result 13


#include <iostream>
#include <future>

#include "remote_executor.hpp"


int hello_world()
{
  std::cout << "PE " << shmem_my_pe() << ": Hello, world!" << std::endl;

  return 13;
}

struct functor
{
  int value;

  int operator()() const
  {
    std::cout << "PE " << shmem_my_pe() << ": Hello, world with value " << value << "!" << std::endl;
    return 13;
  }

  template<class InputArchive>
  friend void deserialize(InputArchive& ar, functor& self)
  {
    deserialize(ar, self.value);
  }

  template<class OutputArchive>
  friend void serialize(OutputArchive& ar, const functor& self)
  {
    serialize(ar, self.value);
  }
};

int main()
{
  if(shmem_my_pe() == 0)
  {
    // execute a function pointer on node 1

    remote_executor exec(1);
    std::future<int> future = exec.twoway_execute(hello_world);

    std::cout << "PE 0: Waiting on future" << std::endl;
    int result = future.get();

    std::cout << "PE 0: Future satisfied with result " << result << std::endl;
  }
  else
  {
    // execute a serializable functor on node 0

    remote_executor exec(0);
    std::future<int> future = exec.twoway_execute(functor{7});

    std::cout << "PE 1: Waiting on future" << std::endl;
    int result = future.get();

    std::cout << "PE 1: Future satisfied with result " << result << std::endl;
  }
}

