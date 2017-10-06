#include <iostream>
#include "process_executor.hpp"

void foo()
{
  std::cout << "foo()" << std::endl;
}

int main()
{
  // call foo() in a newly-created process
  process_executor exec;
  exec.execute(foo);
}

