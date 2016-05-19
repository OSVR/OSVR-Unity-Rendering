call :runfile OsvrRenderingPlugin.cpp

goto :eof
:runfile
clang-tidy -header-filter=OsvrRendering.* -fix -fix-errors %1  -- -Wall -Wextra -Wold-style-cast -I "C:\jenkins-deps\64\osvr-core\include" -I "C:\Users\Ryan\Apps\osvr\rendermanager-64-latest-ci\include"  -I "C:\local\boost_1_60_0" -I "C:\jenkins-deps\glew-1.13.0\include" -I "C:\Users\Ryan\Desktop\deps\jsoncpp-64-vc12\include" --std=c++11
goto :eof