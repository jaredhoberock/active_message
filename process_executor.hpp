#include <limits.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

#include <cstdlib>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cassert>


#include "active_message.hpp"


namespace this_process
{


static inline const std::vector<std::string>& environment()
{
  static std::vector<std::string> result;

  if(result.empty())
  {
    for(char** variable = environ; *variable; ++variable)
    {
      result.push_back(std::string(*variable));
    }
  }

  return result;
}


static inline const std::string& filename()
{
  static std::string result;

  if(result.empty())
  {
    std::string symbolic_name = std::string("/proc/") + std::to_string(getpid()) + "/exe";

    char real_name[PATH_MAX + 1];
    ssize_t length = readlink(symbolic_name.c_str(), real_name, PATH_MAX);
    if(length == -1)
    {
      throw std::runtime_error("this_process::filename(): Error after readlink().");
    }

    real_name[length] = '\0';

    result = real_name;
  }

  return result;
}


}


// this tracks all processes created through process_executors
// and blocks on their completion in its destructor
class process_context
{
  public:
    inline ~process_context()
    {
      wait();
    }

    template<class Function>
    void execute(Function&& f)
    {
      std::lock_guard<std::mutex> lock(mutex_);

      // create an active_message out of f
      active_message message(decay_copy(std::forward<Function>(f)));

      // make a copy of this process's environment and set the variable ALTERNATE_MAIN_ACTIVE_MESSAGE to contain serialized message
      auto spawnee_environment = this_process::environment();
      set_variable(spawnee_environment, "EXECUTE_ACTIVE_MESSAGE_BEFORE_MAIN", to_string(message));
      auto spawnee_environment_view = environment_view(spawnee_environment);

      pid_t spawnee_id;
      char* spawnee_argv[] = {nullptr};
      int error = posix_spawn(&spawnee_id, this_process::filename().c_str(), nullptr, nullptr, spawnee_argv, spawnee_environment_view.data());
      if(error)
      {
        throw std::runtime_error("Error after posix_spawn");
      }

      // keep track of the new process
      processes_.push_back(spawnee_id);
    }

    inline void wait()
    {
      std::lock_guard<std::mutex> lock(mutex_);

      // wait for each spawned process to finish
      for(pid_t p : processes_)
      {
        waitpid(p, nullptr, 0);
      }

      processes_.clear();
    }

  private:
    static inline void set_variable(std::vector<std::string>& environment, const std::string& variable, const std::string& value)
    {
      auto existing_variable = std::find_if(environment.begin(), environment.end(), [&](const std::string& current_variable)
      {
        // check if variable is a prefix of current_variable
        auto result = std::mismatch(variable.begin(), variable.end(), current_variable.begin());
        if(result.first == variable.end())
        {
          // check if the next character after the prefix is an equal sign
          return result.second != variable.end() && *result.second == '=';
        }
    
        return false;
      });
    
      if(existing_variable != environment.end())
      {
        *existing_variable = variable + "=" + value;
      }
      else
      {
        environment.emplace_back(variable + "=" + value);
      }
    }
    
    static inline std::vector<char*> environment_view(const std::vector<std::string>& environment)
    {
      std::vector<char*> result;
    
      for(const std::string& variable : environment)
      {
        result.push_back(const_cast<char*>(variable.c_str()));
      }

      // the view is assumed to be null-terminated
      result.push_back(0);
    
      return result;
    }

    template<class Arg>
    static typename std::decay<Arg>::type decay_copy(Arg&& arg)
    {
      return std::forward<Arg>(arg);
    }

    std::mutex mutex_;
    std::vector<pid_t> processes_;
};

process_context global_process_context;


// this replaces a process's execution of main() with an active_message if
// the environment variable EXECUTE_ACTIVE_MESSAGE_BEFORE_MAIN is defined
struct execute_active_message_before_main_if
{
  execute_active_message_before_main_if()
  {
    char* variable = std::getenv("EXECUTE_ACTIVE_MESSAGE_BEFORE_MAIN");
    if(variable)
    {
      active_message message = from_string<active_message>(variable);
      message.activate();

      std::exit(EXIT_SUCCESS);
    }
  }
};

execute_active_message_before_main_if before_main{};


struct process_executor
{
  template<class Function>
  void execute(Function&& f) const
  {
    global_process_context.execute(std::forward<Function>(f));
  }
};

