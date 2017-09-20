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

