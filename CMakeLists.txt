cmake_minimum_required(VERSION 3.20)

project(QRCodeTool)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 ZXing-CPP 包
find_package(ZXing CONFIG REQUIRED)

# 查找 nayuki-qr-code-generator 包
find_package(unofficial-nayuki-qr-code-generator CONFIG REQUIRED)

# 添加 ZXing 版本的主程序
add_executable(QRCodeTool WIN32 main.cpp)

# 链接库
target_link_libraries(QRCodeTool 
    ZXing::ZXing
    unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator
    gdiplus
    shell32
    user32
    gdi32
)