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
    src/gitservice.cpp
    src/gitservice.h
    src/githubservice.cpp
    src/githubservice.h
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
    src/synopsisservice.cpp
    src/synopsisservice.h
    src/gitpanel.cpp
    src/gitpanel.h
    src/previewpanel.cpp
    src/previewpanel.h
    src/variablespanel.cpp
    src/variablespanel.h
    src/variablemanager.cpp
    src/variablemanager.h
    src/variablecompletionmodel.cpp
    src/variablecompletionmodel.h
    src/projectmanager.cpp
    src/projectmanager.h
    src/projectsettingsdialog.cpp
    src/projectsettingsdialog.h
    src/projecttreemodel.cpp
    src/projecttreemodel.h
    src/projecttreepanel.cpp
    src/projecttreepanel.h
    src/metadatadialog.cpp
    src/metadatadialog.h
    src/documentconverter.cpp
    src/documentconverter.h
    src/scrivenerimporter.cpp
    src/scrivenerimporter.h
    src/llmservice.cpp
    src/llmservice.h
    src/librariandatabase.cpp
    src/librariandatabase.h
    src/librarianservice.cpp
    src/librarianservice.h
    src/characterdossierservice.cpp
    src/characterdossierservice.h
    src/knowledgebase.cpp
    src/knowledgebase.h
    src/agentgatekeeper.cpp
    src/agentgatekeeper.h
    src/analyzerservice.cpp
    src/analyzerservice.h
    src/textutils.h
    src/problemspanel.cpp
    src/problemspanel.h
    src/simulationstate.cpp
    src/simulationstate.h
    src/simulationactor.cpp
    src/simulationactor.h
    src/simarbiter.cpp
    src/simarbiter.h
    src/simgriot.cpp
    src/simgriot.h
    src/simulationpanel.cpp
    src/simulationpanel.h
    src/charactergenerator.cpp
    src/charactergenerator.h
    src/simulationcomparedialog.cpp
    src/simulationcomparedialog.h
    src/simulationmanager.cpp
    src/simulationmanager.h
    src/mcpservice.cpp
    src/mcpservice.h
    src/diceengine.cpp
    src/diceengine.h
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
    src/pdfexporter.cpp
    src/pdfexporter.h
    src/resources.qrc
)

target_link_libraries(rpgforge
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::WebEngineWidgets
    Qt6::Qml
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Xml
    KF6::CoreAddons
    KF6::I18n
    KF6::XmlGui
    KF6::TextEditor
    KF6::KIOWidgets
    KF6::WidgetsAddons
    KF6::Parts
    KF6::Wallet
    PkgConfig::LIBGIT2
    PkgConfig::CMARK_GFM
    ${KOMPAREINTERFACE_LIB}
)

install(TARGETS rpgforge ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES src/rpgforgeui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/rpgforge)
install(FILES org.kde.rpgforge.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES icons/rpgforge.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
install(FILES icons/rpgforge_16x16.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/16x16/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_22x22.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/22x22/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_32x32.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/32x32/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_48x48.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/48x48/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_64x64.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/64x64/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_128x128.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/128x128/apps RENAME rpgforge.png)
install(FILES icons/rpgforge_256x256.png DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/256x256/apps RENAME rpgforge.png)

# Testing
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_librariandatabase tests/test_librariandatabase.cpp src/librariandatabase.cpp)
target_link_libraries(test_librariandatabase Qt6::Test Qt6::Sql Qt6::Core)
add_test(NAME librarian_database_test COMMAND test_librariandatabase)

add_executable(test_librarianservice 
    tests/test_librarianservice.cpp 
    src/librarianservice.cpp 
    src/librariandatabase.cpp
)
target_link_libraries(test_librarianservice 
    Qt6::Test 
    Qt6::Sql 
    Qt6::Core 
    Qt6::Concurrent
    KF6::I18n
)
add_executable(test_projectmanager 
    tests/test_projectmanager.cpp 
    src/projectmanager.cpp
    src/projecttreemodel.cpp
    src/variablemanager.cpp
    src/synopsisservice.cpp
    src/librarianservice.cpp
    src/librariandatabase.cpp
    src/knowledgebase.cpp
    src/llmservice.cpp
    src/mcpservice.cpp
    src/agentgatekeeper.cpp
    src/analyzerservice.cpp
    src/characterdossierservice.cpp
    src/diceengine.cpp
    src/simulationmanager.cpp
    src/simulationstate.cpp
    src/simarbiter.cpp
    src/simgriot.cpp
    src/simulationactor.cpp
)
target_link_libraries(test_projectmanager 
    Qt6::Test 
    Qt6::Sql 
    Qt6::Core 
    Qt6::Concurrent
    Qt6::Xml
    Qt6::Network
    Qt6::Gui
    KF6::CoreAddons
    KF6::I18n
    KF6::TextEditor
    KF6::Wallet
)
add_test(NAME project_manager_test COMMAND test_projectmanager)

add_executable(test_projecttreemodel
    tests/test_projecttreemodel.cpp
    src/projecttreemodel.cpp
    src/synopsisservice.cpp
    src/projectmanager.cpp
    src/variablemanager.cpp
)
target_link_libraries(test_projecttreemodel
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Concurrent
    KF6::I18n
)
add_test(NAME project_tree_model_test COMMAND test_projecttreemodel)

