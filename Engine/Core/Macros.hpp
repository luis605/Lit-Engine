#ifndef MACROS_HPP
#define MACROS_HPP

#ifdef _WIN32
    #define EXPORT_API __declspec(dllexport)
#else
    #define EXPORT_API __attribute__((visibility("default")))
#endif

#endif // MACROS_HPP