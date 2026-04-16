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

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui WebEngineWidgets Qml Concurrent Sql Xml)
find_package(KF6 REQUIRED COMPONENTS
    CoreAddons
    I18n
    XmlGui
    TextEditor
    KIO
    WidgetsAddons
    Parts
    Wallet
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(CMARK_GFM REQUIRED IMPORTED_TARGET libcmark-gfm)

find_library(KOMPAREINTERFACE_LIB kompareinterface REQUIRED)

# Core Library (Shared between app and tests)
add_library(rpgforge_core STATIC
    src/projectmanager.cpp
    src/projectmanager.h
    src/projecttreemodel.cpp
    src/projecttreemodel.h
    src/variablemanager.cpp
    src/variablemanager.h
    src/synopsisservice.cpp
    src/synopsisservice.h
    src/librarianservice.cpp
    src/librarianservice.h
    src/librariandatabase.cpp
    src/librariandatabase.h
    src/llmservice.cpp
    src/llmservice.h
    src/lorekeeperservice.cpp
    src/lorekeeperservice.h
    src/knowledgebase.cpp
    src/knowledgebase.h
    src/agentgatekeeper.cpp
    src/agentgatekeeper.h
    src/analyzerservice.cpp
    src/analyzerservice.h
    src/mcpservice.cpp
    src/mcpservice.h
    src/diceengine.cpp
    src/diceengine.h
    src/simulationstate.cpp
    src/simulationstate.h
    src/simulationactor.cpp
    src/simulationactor.h
    src/simarbiter.cpp
    src/simarbiter.h
    src/simgriot.cpp
    src/simgriot.h
    src/simulationmanager.cpp
    src/simulationmanager.h
    src/charactergenerator.cpp
    src/charactergenerator.h
    src/documentconverter.cpp
    src/documentconverter.h
    src/scrivenerimporter.cpp
    src/scrivenerimporter.h
    src/pdfexporter.cpp
    src/pdfexporter.h
    src/markdownparser.cpp
    src/markdownparser.h
    src/gitstatusmodel.cpp
    src/gitstatusmodel.h
    src/gitservice.cpp
    src/gitservice.h
    src/githubservice.cpp
    src/githubservice.h
    src/variablecompletionmodel.cpp
    src/variablecompletionmodel.h
)

target_link_libraries(rpgforge_core
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Xml
    Qt6::Network
    Qt6::WebEngineWidgets
    KF6::CoreAddons
    KF6::I18n
    KF6::TextEditor
    KF6::KIOWidgets
    KF6::WidgetsAddons
    KF6::Wallet
    PkgConfig::LIBGIT2
    PkgConfig::CMARK_GFM
)

# Main Executable
add_executable(rpgforge
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/fileexplorer.cpp
    src/fileexplorer.h
    src/outlinepanel.cpp
    src/outlinepanel.h
    src/githubonboardingdialog.cpp
    src/githubonboardingdialog.h
    src/historydialog.cpp
    src/historydialog.h
    src/visualdiffview.cpp
    src/visualdiffview.h
    src/clonedialog.cpp
    src/clonedialog.h
    src/breadcrumbbar.cpp
    src/breadcrumbbar.h
    src/sidebar.cpp
    src/sidebar.h
    src/gitpanel.cpp
    src/gitpanel.h
    src/previewpanel.cpp
    src/previewpanel.h
    src/variablespanel.cpp
    src/variablespanel.h
    src/projectsettingsdialog.cpp
    src/projectsettingsdialog.h
    src/projecttreepanel.cpp
    src/projecttreepanel.h
    src/metadatadialog.cpp
    src/metadatadialog.h
    src/problemspanel.cpp
    src/problemspanel.h
    src/simulationpanel.cpp
    src/simulationpanel.h
    src/simulationcomparedialog.cpp
    src/simulationcomparedialog.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/prompteditordialog.cpp
    src/prompteditordialog.h
    src/diagnosticdetaildialog.cpp
    src/diagnosticdetaildialog.h
    src/chatpanel.cpp
    src/chatpanel.h
    src/compiledialog.cpp
    src/compiledialog.h
    src/newprojectdialog.cpp
    src/newprojectdialog.h
    src/onboardingwizard.cpp
    src/onboardingwizard.h
    src/explorersidebar.cpp
    src/explorersidebar.h
    src/imagepreview.cpp
    src/imagepreview.h
    src/corkboardview.cpp
    src/corkboardview.h
    src/corkboardcard.cpp
    src/corkboardcard.h
    src/resources.qrc
)

target_link_libraries(rpgforge
    rpgforge_core
    Qt6::WebEngineWidgets
    ${KOMPAREINTERFACE_LIB}
)

install(TARGETS rpgforge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES src/rpgforgeui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/rpgforge)
install(FILES org.kde.rpgforge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES icons/rpgforge.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)

# Testing
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_librariandatabase tests/test_librariandatabase.cpp)
target_link_libraries(test_librariandatabase rpgforge_core Qt6::Test)
add_test(NAME librarian_database_test COMMAND test_librariandatabase)

add_executable(test_librarian tests/test_librarian.cpp)
target_link_libraries(test_librarian rpgforge_core Qt6::Test)
add_test(NAME librarian_test COMMAND test_librarian)

add_executable(test_librarianservice tests/test_librarianservice.cpp)
target_link_libraries(test_librarianservice rpgforge_core Qt6::Test)
add_test(NAME librarian_service_test COMMAND test_librarianservice)

add_executable(test_projectmanager tests/test_projectmanager.cpp)
target_link_libraries(test_projectmanager rpgforge_core Qt6::Test)
add_test(NAME project_manager_test COMMAND test_projectmanager)

add_executable(test_lorekeeper_smoke tests/test_lorekeeper_smoke.cpp)
target_link_libraries(test_lorekeeper_smoke rpgforge_core Qt6::Test)
add_test(NAME lorekeeper_smoke_test COMMAND test_lorekeeper_smoke)

add_executable(test_projecttreemodel tests/test_projecttreemodel.cpp)
target_link_libraries(test_projecttreemodel rpgforge_core Qt6::Test)
add_test(NAME project_tree_model_test COMMAND test_projecttreemodel)
