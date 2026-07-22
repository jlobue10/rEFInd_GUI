cmake_minimum_required(VERSION 3.16)

project(rEFInd_GUI VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network LinguistTools)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network LinguistTools)

# rEFInd_GUI_en_US.ts is the source-language reference; the others carry the
# actual translations. Refresh all of them from the sources with the
# update_translations target (Qt 6) or lupdate directly — see I18N_AUDIT.md.
set(TS_FILES
    rEFInd_GUI_en_US.ts
    rEFInd_GUI_ar.ts
    rEFInd_GUI_bn.ts
    rEFInd_GUI_de.ts
    rEFInd_GUI_es.ts
    rEFInd_GUI_fa.ts
    rEFInd_GUI_fr.ts
    rEFInd_GUI_hi.ts
    rEFInd_GUI_id.ts
    rEFInd_GUI_it.ts
    rEFInd_GUI_ja.ts
    rEFInd_GUI_ko.ts
    rEFInd_GUI_pt.ts
    rEFInd_GUI_ru.ts
    rEFInd_GUI_scn.ts
    rEFInd_GUI_tr.ts
    rEFInd_GUI_uk.ts
    rEFInd_GUI_ur.ts
    rEFInd_GUI_vi.ts
    rEFInd_GUI_zh_CN.ts
)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        osdetect.h
        osdetect_common.cpp
        platform.cpp
        platform.h
        previewdialog.cpp
        previewdialog.h
        uitranslation.cpp
        uitranslation.h
        resources.qrc
        ${TS_FILES}
)

if(WIN32)
    enable_language(RC)
    list(APPEND PROJECT_SOURCES osdetect_win.cpp rEFInd_GUI.rc)
else()
    list(APPEND PROJECT_SOURCES osdetect_linux.cpp)
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(rEFInd_GUI
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET rEFInd_GUI APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    # Compile the .ts files at build time and embed the .qm output under the
    # ":/i18n" resource prefix (the default) that main.cpp loads from. The old
    # qt_create_translation() call left QM_FILES dangling with no dependent
    # target, so no .qm was ever built and the app always ran untranslated.
    qt_add_translations(rEFInd_GUI TS_FILES ${TS_FILES})

    # Embed Qt's own qtbase catalogs for the shipped languages so standard
    # dialog buttons are translated even where no qt6-translations package
    # exists (SteamOS); UiTranslation loads ":/i18n_qtbase" as the last
    # fallback after the Qt install and windeployqt's translations/ dir.
    set(_qtbase_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    set(_qtbase_entries "")
    foreach(_lang ar bn de es fa fr hi id it ja ko pt ru tr uk ur vi zh_CN)
        if(EXISTS "${_qtbase_dir}/qtbase_${_lang}.qm")
            string(APPEND _qtbase_entries "  <file alias=\"qtbase_${_lang}.qm\">${_qtbase_dir}/qtbase_${_lang}.qm</file>\n")
        endif()
    endforeach()
    if(_qtbase_entries)
        set(_qtbase_qrc "${CMAKE_CURRENT_BINARY_DIR}/qtbase_translations.qrc")
        file(WRITE "${_qtbase_qrc}"
             "<RCC>\n <qresource prefix=\"/i18n_qtbase\">\n${_qtbase_entries} </qresource>\n</RCC>\n")
        target_sources(rEFInd_GUI PRIVATE "${_qtbase_qrc}")
    else()
        message(WARNING "No qtbase_*.qm found under ${_qtbase_dir} - standard dialog buttons will rely on system Qt translations")
    endif()
else()
    if(ANDROID)
        add_library(rEFInd_GUI SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(rEFInd_GUI
            ${PROJECT_SOURCES}
        )
    endif()

    # Qt 5 has no qt_add_translations: compile the .qm files (lrelease only —
    # qt5_create_translation would also rewrite the .ts files in the source
    # tree on every build) into translations/ next to the binary, where
    # main.cpp's filesystem fallback picks them up.
    set_source_files_properties(${TS_FILES} PROPERTIES
        OUTPUT_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/translations)
    qt5_add_translation(QM_FILES ${TS_FILES})
    add_custom_target(rEFInd_GUI_translations ALL DEPENDS ${QM_FILES})
endif()

target_link_libraries(rEFInd_GUI PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

if(MSVC)
    # MinGW gets the manifest via rEFInd_GUI.rc; MSVC generates its own.
    target_link_options(rEFInd_GUI PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

set_target_properties(rEFInd_GUI PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS rEFInd_GUI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


# Optional developer unit tests (Linux only; OFF so packaging builds are
# unaffected): cmake -DBUILD_GUI_TESTS=ON, then ctest --test-dir <builddir>.
option(BUILD_GUI_TESTS "Build the developer unit tests" OFF)
if(BUILD_GUI_TESTS AND NOT WIN32)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(tst_osdetect tests/tst_osdetect.cpp osdetect_common.cpp osdetect_linux.cpp platform.cpp)
    target_include_directories(tst_osdetect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_osdetect PRIVATE
        Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets)
    add_test(NAME osdetect COMMAND tst_osdetect)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(rEFInd_GUI)
endif()
