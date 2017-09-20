#pragma once

#include <istream>

template<class CharT>
class basic_string_view_stream : public std::basic_istream<CharT>
{
  public:
    basic_string_view_stream(const CharT* data, std::size_t size)
      : std::istream(),
        buffer_(data, size)
    {
      // pass buffer_ to basic_istream *after* buffer_ has been constructed
      std::basic_istream<CharT>::rdbuf(&buffer_);
    }

    virtual ~basic_string_view_stream(){}

  private:
    class string_view_buffer : public std::streambuf
    {
      public:
        using char_type = char;
        using traits_type = std::char_traits<char_type>;
        using int_type = typename traits_type::int_type;
        using pos_type = typename traits_type::pos_type;
        using off_type = typename traits_type::off_type;
    
        string_view_buffer(const char* data, std::size_t size)
          : begin_(data),
            end_(data + size),
            current_(data)
        {}
    
        string_view_buffer(const string_view_buffer&) = delete;
    
      protected:
        const char_type* begin_;
        const char_type* end_;
        const char_type* current_;
    
        int_type underflow() override
        {
          if(current_ == end_)
          {
            return traits_type::eof();
          }
    
          return traits_type::to_int_type(*current_);
        }
    
        int_type uflow() override
        {
          if(current_ == end_)
          {
            return traits_type::eof();
          }
    
          return traits_type::to_int_type(*current_++);
        }
    
        int_type pbackfail(int_type c) override
        {
          if(current_ == begin_ || (c != traits_type::eof() && c != current_[-1]))
          {
            return traits_type::eof();
          }
    
          return traits_type::to_int_type(*--current_);
        }
    
        std::streamsize showmanyc() override
        {
          return end_ - current_;
        }
    };

    string_view_buffer buffer_;
};

using string_view_stream = basic_string_view_stream<char>;

