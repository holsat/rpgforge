cmake_minimum_required(VERSION 3.20)
project(rpgforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
find_package(KF6 REQUIRED COMPONENTS
    CoreAddons
    I18n
    XmlGui
    TextEditor
    KIO
    WidgetsAddons
    Parts
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(CMARK_GFM REQUIRED IMPORTED_TARGET libcmark-gfm)

add_executable(rpgforge
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/fileexplorer.cpp
    src/fileexplorer.h
    src/gitstatusmodel.cpp
    src/gitstatusmodel.h
    src/markdownparser.cpp
    src/markdownparser.h
    src/outlinepanel.cpp
    src/outlinepanel.h
    src/breadcrumbbar.cpp
    src/breadcrumbbar.h
    src/sidebar.cpp
    src/sidebar.h
    src/gitpanel.cpp
    src/gitpanel.h
    src/resources.qrc
)

target_link_libraries(rpgforge
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    KF6::CoreAddons
    KF6::I18n
    KF6::XmlGui
    KF6::TextEditor
    KF6::KIOWidgets
    KF6::WidgetsAddons
    KF6::Parts
    PkgConfig::LIBGIT2
    PkgConfig::CMARK_GFM
)

install(TARGETS rpgforge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES src/rpgforgeui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/rpgforge)
install(FILES rpgforge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES icons/rpgforge.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
install(FILES icons/rpgforge_16x16.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/16x16/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_22x22.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/22x22/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_32x32.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/32x32/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_48x48.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/48x48/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_64x64.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/64x64/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_128x128.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/128x128/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_256x256.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/256x256/apps RENAME rpgforge.png)
