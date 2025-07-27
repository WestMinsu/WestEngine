#pragma once
#include <iostream>

#ifdef _DEBUG
#define FILENAME (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define WEST_INFO(x) std::cout << "[INFO] " << "[" << FILENAME << ":" << __LINE__ << "] " << x << std::endl
#define WEST_ERR(x) std::cerr << "[ERR] " << "[" << FILENAME << ":" << __LINE__ << "] " << x << std::endl

#else

#define WEST_INFO(x) do {} while (0)
#define WEST_ERR(x) do {} while (0)

#endif
