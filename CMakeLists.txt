cmake_minimum_required(VERSION 3.10)

# שם הפרויקט והשפה
project(CryptoArbitrageEngine CXX)

# שימוש בסטנדרט C++20 (הכי חדיש, חשוב לביצועים)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# הגדרת תיקיית ה-include (איפה שישבו קובצי ה-.hpp שלנו)
include_directories(include)

# יצירת קובץ הריצה מתוך קובץ המקור
add_executable(ArbitrageEngine src/main.cpp)