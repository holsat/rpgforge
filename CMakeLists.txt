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

# DBus-exposed test automation interface. ON by default for Debug builds,
# OFF for Release — the DBus adaptor and its registration in main.cpp are
# compiled out entirely when this is OFF, so production binaries don't
# expose an IPC surface.
if(NOT DEFINED RPGFORGE_DBUS_TESTING)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(RPGFORGE_DBUS_TESTING_DEFAULT ON)
    else()
        set(RPGFORGE_DBUS_TESTING_DEFAULT OFF)
    endif()
endif()
option(RPGFORGE_DBUS_TESTING
    "Expose org.kde.rpgforge DBus adaptor for automated GUI testing"
    ${RPGFORGE_DBUS_TESTING_DEFAULT})

# Diagnostic RPGFORGE_DLOG() macros (src/debuglog.h) compile to real
# qDebug() calls when ON, and to no-ops otherwise. Leave per-feature debug
# tracing in the code permanently; it vanishes from production binaries.
if(NOT DEFINED RPGFORGE_DEBUG_LOGS)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(RPGFORGE_DEBUG_LOGS_DEFAULT ON)
    else()
        set(RPGFORGE_DEBUG_LOGS_DEFAULT OFF)
    endif()
endif()
option(RPGFORGE_DEBUG_LOGS
    "Enable RPGFORGE_DLOG() diagnostic logging (recommended for Debug builds)"
    ${RPGFORGE_DEBUG_LOGS_DEFAULT})

set(RPGFORGE_QT_COMPONENTS Widgets Core Gui WebEngineWidgets Qml Concurrent Sql Xml)
if(RPGFORGE_DBUS_TESTING)
    list(APPEND RPGFORGE_QT_COMPONENTS DBus)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${RPGFORGE_QT_COMPONENTS})
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
    src/projectmetadata.cpp
    src/projectmetadata.h
    src/reconciliationtypes.h
    src/debuglog.h
    src/ragassistservice.cpp
    src/ragassistservice.h
    src/inlineaiinvoker.cpp
    src/inlineaiinvoker.h
    src/inlineaicompletionmodel.cpp
    src/inlineaicompletionmodel.h
    src/treenodesnapshot.cpp
    src/treenodesnapshot.h
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

# RPGFORGE_DEBUG_LOGS: propagates to every target consuming rpgforge_core
# so RPGFORGE_DLOG() in any translation unit sees the same compile-time
# state. PUBLIC because headers (src/debuglog.h, and its transitive includers)
# branch on the define.
if(RPGFORGE_DEBUG_LOGS)
    target_compile_definitions(rpgforge_core PUBLIC RPGFORGE_DEBUG_LOGS=1)
endif()

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
    src/versionrecallbrowser.cpp
    src/versionrecallbrowser.h
    src/unsavedchangesdialog.cpp
    src/unsavedchangesdialog.h
    src/explorationsgraphview.cpp
    src/explorationsgraphview.h
    src/explorationspanel.cpp
    src/explorationspanel.h
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
    src/reconciliationdialog.cpp
    src/reconciliationdialog.h
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
    src/toggleswitch.cpp
    src/toggleswitch.h
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

if(RPGFORGE_DBUS_TESTING)
    target_sources(rpgforge PRIVATE
        src/rpgforgedbus.cpp
        src/rpgforgedbus.h
    )
    target_compile_definitions(rpgforge PRIVATE RPGFORGE_DBUS_TESTING=1)
    target_link_libraries(rpgforge Qt6::DBus)
endif()

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

add_executable(test_projectmanager_mutations tests/test_projectmanager_mutations.cpp)
target_link_libraries(test_projectmanager_mutations rpgforge_core Qt6::Test)
add_test(NAME project_manager_mutations_test COMMAND test_projectmanager_mutations)

add_executable(test_projectmanager_disk_authority tests/test_projectmanager_disk_authority.cpp)
target_link_libraries(test_projectmanager_disk_authority rpgforge_core Qt6::Test)
add_test(NAME project_manager_disk_authority_test COMMAND test_projectmanager_disk_authority)

add_executable(test_reconciliation tests/test_reconciliation.cpp)
target_link_libraries(test_reconciliation rpgforge_core Qt6::Test)
add_test(NAME reconciliation_test COMMAND test_reconciliation)

add_executable(test_projectmetadata tests/test_projectmetadata.cpp)
target_link_libraries(test_projectmetadata rpgforge_core Qt6::Test)
add_test(NAME project_metadata_test COMMAND test_projectmetadata)

add_executable(test_treesnapshot tests/test_treesnapshot.cpp)
target_link_libraries(test_treesnapshot rpgforge_core Qt6::Test)
add_test(NAME tree_snapshot_test COMMAND test_treesnapshot)

add_executable(test_lorekeeper_smoke tests/test_lorekeeper_smoke.cpp)
target_link_libraries(test_lorekeeper_smoke rpgforge_core Qt6::Test)
add_test(NAME lorekeeper_smoke_test COMMAND test_lorekeeper_smoke)

add_executable(test_ragassistservice tests/test_ragassistservice.cpp)
target_link_libraries(test_ragassistservice rpgforge_core Qt6::Test)
add_test(NAME rag_assist_service_test COMMAND test_ragassistservice)

add_executable(test_projecttreemodel tests/test_projecttreemodel.cpp)
target_link_libraries(test_projecttreemodel rpgforge_core Qt6::Test)
add_test(NAME project_tree_model_test COMMAND test_projecttreemodel)

add_executable(test_fswatcher tests/test_fswatcher.cpp)
target_link_libraries(test_fswatcher rpgforge_core Qt6::Test)
add_test(NAME fs_watcher_test COMMAND test_fswatcher)

# Explorations feature tests
add_executable(test_gitservice tests/test_gitservice.cpp)
target_link_libraries(test_gitservice rpgforge_core Qt6::Test)
add_test(NAME git_service_test COMMAND test_gitservice)

add_executable(test_explorationgraphview
    tests/test_explorationgraphview.cpp
    src/explorationsgraphview.cpp
    src/explorationsgraphview.h
)
target_link_libraries(test_explorationgraphview rpgforge_core Qt6::Test Qt6::Widgets)
add_test(NAME exploration_graph_view_test COMMAND test_explorationgraphview)
set_tests_properties(exploration_graph_view_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(test_unsavedchangesdialog
    tests/test_unsavedchangesdialog.cpp
    src/unsavedchangesdialog.cpp
    src/unsavedchangesdialog.h
)
target_link_libraries(test_unsavedchangesdialog rpgforge_core Qt6::Test Qt6::Widgets KF6::I18n)
add_test(NAME unsaved_changes_dialog_test COMMAND test_unsavedchangesdialog)
set_tests_properties(unsaved_changes_dialog_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
