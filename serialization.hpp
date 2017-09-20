#pragma once

#include <iostream>
#include <tuple>
#include <string>


#define __REQUIRES(...) typename std::enable_if<(__VA_ARGS__)>::type* = nullptr


// this serialization scheme is based on Cereal
// see http://uscilab.github.io/cereal

template<class OutputArchive, class T>
void serialize(OutputArchive& ar, const T& value)
{
  // by default, use formatted output, and follow with whitespace
  ar.stream() << value << " ";
}

template<class OutputArchive, class Result, class... Args>
void serialize(OutputArchive& ar, Result (*const &fun_ptr)(Args...))
{
  void* void_ptr = reinterpret_cast<void*>(fun_ptr);

  serialize(ar, void_ptr);
}

template<class OutputArchive>
void serialize(OutputArchive& ar, const std::string& s)
{
  // output the length
  serialize(ar, s.size());

  // output the bytes
  ar.stream().write(s.data(), s.size());
}


template<class InputArchive, class T>
void deserialize(InputArchive& ar, T& value)
{
  // by default, use formatted input, and consume trailing whitespace
  ar.stream() >> value >> std::ws;
}

template<class InputArchive, class Result, class... Args>
void deserialize(InputArchive& ar, Result (*&fun_ptr)(Args...))
{
  void* void_ptr = nullptr;
  deserialize(ar, void_ptr);

  using function_ptr_type = Result (*)(Args...);
  fun_ptr = reinterpret_cast<function_ptr_type>(void_ptr);
}


template<class InputArchive>
void deserialize(InputArchive& ar, std::string& s)
{
  // read the length and resize the string
  std::size_t length = 0;
  deserialize(ar, length);
  s.resize(length);

  // read characters from the stream
  ar.stream().read(&s.front(), length);
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
  private:
    // this is the terminal case of operator() above
    // it never needs to be called by a client
    inline void operator()() {}

    std::ostream& stream_;

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

      (*this)(args...);
    }

    inline std::ostream& stream()
    {
      return stream_;
    }
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


class any
{
  public:
    any() = default;

    template<class T>
    any(T&& value)
    {
      std::stringstream os;

      {
        output_archive archive(os);

        archive(value);
      }

      representation_ = os.str();
    }

    template<class ValueType>
    friend ValueType any_cast(const any& self)
    {
      ValueType result;

      std::stringstream is(self.representation_);
      input_archive archive(is);

      archive(result);

      return result;
    }

    const char* data() const
    {
      return representation_.data();
    }

    size_t size() const
    {
      return representation_.size();
    }

  private:
    std::string representation_;
};


template<class OutputArchive>
void serialize(OutputArchive& ar, const any& a)
{
  ar.stream() << a;
}

template<class InputArchive>
void deserialize(InputArchive& ar, any& a)
{
  ar.stream() >> a;
}


class serializable_closure
{
  private:
    static void noop_function() {}

  public:
    serializable_closure()
      : serializable_closure(&noop_function)
    {}

    template<class FunctionPtr, class... Args>
    explicit serializable_closure(FunctionPtr func, Args... args)
      : serialized_(serialize_function_and_arguments(&deserialize_and_invoke<FunctionPtr,Args...>, func, args...))
    {}

    size_t size() const
    {
      return serialized_.size();
    }

    const char* data() const
    {
      return serialized_.data();
    }

    any operator()() const
    {
      std::stringstream is(serialized_);
      input_archive archive(is);

      // extract a function_ptr_type from the beginning of the buffer
      using function_ptr_type = any (*)(input_archive&);
      function_ptr_type invoke_me = nullptr;
      archive(invoke_me);

      // invoke the function pointer on the remaining data
      return invoke_me(archive);
    }

    template<class OutputArchive>
    friend void serialize(OutputArchive& ar, const serializable_closure& sc)
    {
      ar(sc.serialized_);
    }

    template<class InputArchive>
    friend void deserialize(InputArchive& ar, serializable_closure& sc)
    {
      ar(sc.serialized_);
    }

  private:
    template<size_t... Indices, class T, class... Ts>
    static std::tuple<Ts...> tail_impl(std::index_sequence<Indices...>, const std::tuple<T,Ts...>& t)
    {
      return std::tuple<Ts...>(std::get<1 + Indices>(t)...);
    }
    
    template<class T, class... Ts>
    static std::tuple<Ts...> tail(const std::tuple<T,Ts...>& t)
    {
      return tail_impl(std::make_index_sequence<sizeof...(Ts)>(), t);
    }
    
    template<size_t... Indices, class Function, class Tuple>
    static auto apply_impl(std::index_sequence<Indices...>, Function&& f, Tuple&& t)
    {
      return std::forward<Function>(f)(std::get<Indices>(std::forward<Tuple>(t))...);
    }
    
    template<class Function, class Tuple>
    static auto apply(Function&& f, Tuple&& t)
    {
      static constexpr size_t num_args = std::tuple_size<std::decay_t<Tuple>>::value;
      return apply_impl(std::make_index_sequence<num_args>(), std::forward<Function>(f), std::forward<Tuple>(t));
    }

    template<class Function, class Tuple,
             class ApplyResult = decltype(apply(std::declval<Function&&>(), std::declval<Tuple&&>())),
             __REQUIRES(std::is_void<ApplyResult>::value)
            >
    static any apply_and_return_any(Function&& f, Tuple&& t)
    {
      apply(std::forward<Function>(f), std::forward<Tuple>(t));
      return any();
    }

    template<class Function, class Tuple,
             class ApplyResult = decltype(apply(std::declval<Function&&>(), std::declval<Tuple&&>())),
             __REQUIRES(!std::is_void<ApplyResult>::value)
            >
    static any apply_and_return_any(Function&& f, Tuple&& t)
    {
      return apply(std::forward<Function>(f), std::forward<Tuple>(t));
    }

    template<class FunctionPtr, class... Args>
    static any deserialize_and_invoke(input_archive& archive)
    {
      // deserialize function pointer and its arguments
      std::tuple<FunctionPtr,Args...> function_and_args;
      archive(function_and_args);

      // split tuple into a function pointer and arguments
      FunctionPtr f = std::get<0>(function_and_args);
      std::tuple<Args...> arguments = tail(function_and_args);

      return apply_and_return_any(f, arguments);
    }

    template<class... Args>
    static std::string serialize_function_and_arguments(const Args&... args)
    {
      std::stringstream os;

      {
        output_archive archive(os);

        // serialize arguments into the archive
        archive(args...);
      }

      return os.str();
    }

    std::string serialized_;
};

