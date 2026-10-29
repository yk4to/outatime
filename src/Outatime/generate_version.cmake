# Writes version.h from version.h.in with VERSION and the current git commit.
# Runs at configure time and before every build, so the commit stays current;
# configure_file leaves the file untouched when nothing changed, so it doesn't
# trigger a rebuild.
#
# Inputs: VERSION ("major.minor.patch"), INPUT, OUTPUT, SOURCE_DIR, GIT_EXECUTABLE (may be empty).

string(REPLACE "." ";" parts "${VERSION}")
list(GET parts 0 VERSION_MAJOR)
list(GET parts 1 VERSION_MINOR)
list(GET parts 2 VERSION_PATCH)

set(COMMIT "")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        set(COMMIT "")
    endif()
endif()

# Same format as the .NET SDK's informational version for the settings app.
if(COMMIT)
    set(INFORMATIONAL_VERSION "${VERSION}+${COMMIT}")
else()
    set(INFORMATIONAL_VERSION "${VERSION}")
endif()

configure_file("${INPUT}" "${OUTPUT}" @ONLY)
