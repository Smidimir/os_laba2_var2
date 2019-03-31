#pragma once

#include <iostream>
#include <memory>
#include <functional>
#include <string>
#include <chrono>

/****************
 * print_stream *
 ****************/

template<typename T, typename...Ts>
void print_stream(T& stream, Ts&&...args)
{
    (stream << ... << std::forward<Ts>(args)) << std::endl;
}

template<typename...Ts>
void print_std(Ts&&...args)
{
    print_stream(std::cout, std::forward<Ts>(args) ...);
}

template<typename...Ts>
void print_err(Ts&&...args)
{
    print_stream(std::cerr, std::forward<Ts>(args) ...);
}

template<typename...Ts>
int print_error(int error_number, Ts&&...args)
{
    print_err("Error ", error_number, ": ", std::forward<Ts>(args) ...);
    return error_number;
}


/********************
 * createRaiiObject *
 ********************/

template<typename T>
auto createRaiiObject(std::function<bool(T*)> initializer, std::function<void(T*)> deinitializer)
{
    return std::unique_ptr<T, std::function<void(T*)>>
            {
                    [initializer]() -> T*
                    {
                        auto ptr = new T{};
                        if (ptr && initializer(ptr))
                            return ptr;
                        return nullptr;
                    } ()
                    , [deinitializer](T* ptr)
                    {
                        if (ptr)
                        {
                            deinitializer(ptr);
                            delete ptr;
                        }
                    }
            };
}

/***************
 * from_string *
 ***************/

template<typename T>
struct text;

#define DEFINE_FROM_STRING(_FUNC_) \
template<> \
struct text<decltype(_FUNC_(std::string{}))> \
{ \
	static decltype(_FUNC_(std::string{})) from_string(std::string const& str) \
	{ \
		return _FUNC_(str); \
	} \
};

DEFINE_FROM_STRING(std::stoi)
DEFINE_FROM_STRING(std::stol)
DEFINE_FROM_STRING(std::stoll)
DEFINE_FROM_STRING(std::stoul)
DEFINE_FROM_STRING(std::stoull)
DEFINE_FROM_STRING(std::stof)
DEFINE_FROM_STRING(std::stod)
DEFINE_FROM_STRING(std::stold)

#undef DEFINE_FROM_STRING

template<typename T>
T from_string(std::string const& str)
{
    return text<T>::from_string(str);
}

/*****************
 * exec_duration *
 *****************/

template <typename TDuration = std::chrono::milliseconds, typename TFunc, typename...TArgs>
TDuration exec_duration(TFunc &&func, TArgs...args)
{
    auto const start = std::chrono::high_resolution_clock::now();

    func(std::forward<TArgs>(args)...);

    auto const finish = std::chrono::high_resolution_clock::now();

    return std::chrono::duration_cast<TDuration>(finish - start);
}

/*****************
 * exec_duration *
 *****************/