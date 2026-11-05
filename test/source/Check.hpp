#ifndef PHOX_TEST_CHECK_HPP
#define PHOX_TEST_CHECK_HPP

#include <cstdlib>
#include <iostream>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

// Passes when `expr` throws an exception of type `type` whose what() contains `text`.
#define CHECK_THROWS_WITH(expr, type, text)                                               \
    do {                                                                                  \
        bool phoxThrown = false;                                                          \
        try {                                                                             \
            (void)(expr);                                                                 \
        } catch (const type &phoxError) {                                                 \
            phoxThrown = std::string(phoxError.what()).find(text) != std::string::npos;   \
        }                                                                                 \
        if (!phoxThrown) {                                                                \
            std::cerr << "CHECK_THROWS_WITH failed: " #expr " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

#endif // PHOX_TEST_CHECK_HPP
