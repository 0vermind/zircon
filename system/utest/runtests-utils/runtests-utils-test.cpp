// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtests-utils-test-globals.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

namespace runtests {
namespace {

static constexpr char kOutputFileName[] = "output.txt";
static constexpr char kEchoSuccessAndArgs[] = "echo Success! $@";
static constexpr char kEchoFailureAndArgs[] = "echo Failure!  $@ 1>&2\nexit 77";
static constexpr size_t kOneMegabyte = 1 << 20;

// Creates a script file with given contents in its constructor and deletes it
// in its destructor.
class ScopedScriptFile {

public:
    // |path| is the path of the file to be created. Should start with kMemFsPath.
    // |contents| are the script contents. Shebang line will be added automatically.
    ScopedScriptFile(const fbl::StringPiece path, const fbl::StringPiece contents);
    ~ScopedScriptFile();
    fbl::StringPiece path() const;

private:
    const fbl::StringPiece path_;
};

ScopedScriptFile::ScopedScriptFile(
    const fbl::StringPiece path, const fbl::StringPiece contents)
    : path_(path) {
    const int fd = open(path_.data(), O_CREAT | O_WRONLY, S_IRWXU);
    ZX_ASSERT_MSG(-1 != fd, "%s", strerror(errno));
    ZX_ASSERT(sizeof(kScriptShebang) == static_cast<size_t>(
                                            write(fd, kScriptShebang, sizeof(kScriptShebang))));
    ZX_ASSERT(contents.size() == static_cast<size_t>(write(fd, contents.data(), contents.size())));
    ZX_ASSERT_MSG(-1 != close(fd), "%s", strerror(errno));
}

ScopedScriptFile::~ScopedScriptFile() {
    remove(path_.data());
}

fbl::StringPiece ScopedScriptFile::path() const {
    return path_;
}


// Creates a subdirectory of TestFsRoot() in its constructor and deletes it in
// its destructor.
class ScopedTestDir {

public:
    ScopedTestDir() : path_(GetNextTestDir()) {
        if (mkdir(path_.c_str(), 0755)) {
            printf("FAILURE: mkdir failed to open %s: %s\n",
                   path_.c_str(), strerror(errno));
            exit(1);
        }
    }
    ~ScopedTestDir() {
        CleanUpDir(path_.c_str());
    }
    const char* path() { return path_.c_str(); }

private:
    // Returns a unique subdirectory of TestFsRoot().
    fbl::String GetNextTestDir() {
        char dir_num[64];  // More than big enough to print INT_MAX.
        sprintf(dir_num, "%d", num_test_dirs_created_++);
        return JoinPath(TestFsRoot(), dir_num);
    }

    // Recursively removes the directory at |dir_path| and its contents.
    static void CleanUpDir(const char* dir_path) {
        struct dirent* entry;
        DIR* dp;

        dp = opendir(dir_path);
        if (dp == nullptr) {
            // File found; remove it.
            remove(dir_path);
            return;
        }

        while ((entry = readdir(dp))) {
            // Skip "." and "..".
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }
            fbl::String sub_dir_name = JoinPath(dir_path, entry->d_name);
            CleanUpDir(sub_dir_name.c_str());
        }
        closedir(dp);

        // Directory is now empty: remove it.
        rmdir(dir_path);
    }

    const fbl::String path_;

    // Used to generate unique subdirectories of TestFsRoot().
    static int num_test_dirs_created_;
};

int ScopedTestDir::num_test_dirs_created_ = 0;

// Returns the number of files or subdirectories in a given directory.
inline int NumEntriesInDir(const char* dir_path) {
    struct dirent* entry;
    int num_entries = 0;
    DIR* dp;

    if (!(dp = opendir(dir_path))) {
        // dir_path actually points to a file. Return -1 by convention.
        return -1;
    }
    while ((entry = readdir(dp))) {
        // Skip "." and "..".
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        ++num_entries;
    }
    closedir(dp);
    return num_entries;
}

// This ensures that ScopedTestDir and ScopedScriptFile, which we make heavy
// use of in these tests, are indeed scoped and tear down without error.
bool ScopedDirsAndFilesAreIndeedScoped() {
    BEGIN_TEST;

    // Entering a test case, test_dir.path() should be empty.
    EXPECT_EQ(0, NumEntriesInDir(TestFsRoot()));

    {
        ScopedTestDir dir;
        EXPECT_EQ(1, NumEntriesInDir(TestFsRoot()));
        EXPECT_EQ(0, NumEntriesInDir(dir.path()));
        {
            fbl::String file_name1 = JoinPath(dir.path(), "a.sh");
            ScopedScriptFile file1(file_name1, "A");
            EXPECT_EQ(1, NumEntriesInDir(dir.path()));
            {
                fbl::String file_name2 = JoinPath(dir.path(), "b.sh");
                ScopedScriptFile file2(file_name2, "B");
                EXPECT_EQ(2, NumEntriesInDir(dir.path()));
            }
            EXPECT_EQ(1, NumEntriesInDir(dir.path()));
        }
        EXPECT_EQ(0, NumEntriesInDir(dir.path()));
    }

    EXPECT_EQ(0, NumEntriesInDir(TestFsRoot()));

    {
        ScopedTestDir dir1;
        ScopedTestDir dir2;
        ScopedTestDir dir3;
        EXPECT_EQ(3, NumEntriesInDir(TestFsRoot()));
    }

    EXPECT_EQ(0, NumEntriesInDir(TestFsRoot()));

    END_TEST;
}

bool ParseTestNamesEmptyStr() {
    BEGIN_TEST;

    fbl::String input("");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    EXPECT_EQ(0, parsed.size());

    END_TEST;
}

bool ParseTestNamesEmptyStrInMiddle() {
    BEGIN_TEST;

    fbl::String input("a,,b");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    ASSERT_EQ(2, parsed.size());
    EXPECT_STR_EQ("a", parsed[0].c_str());
    EXPECT_STR_EQ("b", parsed[1].c_str());

    END_TEST;
}

bool ParseTestNamesTrailingComma() {
    BEGIN_TEST;

    fbl::String input("a,");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    ASSERT_EQ(1, parsed.size());
    EXPECT_STR_EQ("a", parsed[0].c_str());

    END_TEST;
}

bool ParseTestNamesNormal() {
    BEGIN_TEST;

    fbl::String input("a,b");
    fbl::Vector<fbl::String> parsed;
    ParseTestNames(input, &parsed);
    ASSERT_EQ(2, parsed.size());
    EXPECT_STR_EQ("a", parsed[0].c_str());
    EXPECT_STR_EQ("b", parsed[1].c_str());

    END_TEST;
}

bool EmptyWhitelist() {
    BEGIN_TEST;

    fbl::Vector<fbl::String> whitelist;
    EXPECT_FALSE(IsInWhitelist("a", whitelist));

    END_TEST;
}

bool NonemptyWhitelist() {
    BEGIN_TEST;

    fbl::Vector<fbl::String> whitelist = {"b", "a"};
    EXPECT_TRUE(IsInWhitelist("a", whitelist));

    END_TEST;
}

bool JoinPathNoTrailingSlash() {
    BEGIN_TEST;

    EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b", "c/d").c_str());

    END_TEST;
}

bool JoinPathTrailingSlash() {
    BEGIN_TEST;

    EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b/", "c/d").c_str());

    END_TEST;
}

bool JoinPathAbsoluteChild() {
    BEGIN_TEST;

    EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b/", "/c/d").c_str());

    END_TEST;
}

bool MkDirAllTooLong() {
    BEGIN_TEST;

    char too_long[PATH_MAX + 2];
    memset(too_long, 'a', PATH_MAX + 1);
    too_long[PATH_MAX + 1] = '\0';
    EXPECT_EQ(ENAMETOOLONG, MkDirAll(too_long));

    END_TEST;
}
bool MkDirAllAlreadyExists() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String already = JoinPath(test_dir.path(), "already");
    const fbl::String exists = JoinPath(already, "exists");
    ASSERT_EQ(0, mkdir(already.c_str(), 0755));
    ASSERT_EQ(0, mkdir(exists.c_str(), 0755));
    EXPECT_EQ(0, MkDirAll(exists));

    END_TEST;
}
bool MkDirAllParentAlreadyExists() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String parent = JoinPath(test_dir.path(), "existing-parent");
    const fbl::String child = JoinPath(parent, "child");
    ASSERT_EQ(0, mkdir(parent.c_str(), 0755));
    EXPECT_EQ(0, MkDirAll(child));
    struct stat s;
    EXPECT_EQ(0, stat(child.c_str(), &s));

    END_TEST;
}
bool MkDirAllParentDoesNotExist() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String parent = JoinPath(test_dir.path(), "not-existing-parent");
    const fbl::String child = JoinPath(parent, "child");
    struct stat s;
    ASSERT_NE(0, stat(parent.c_str(), &s));
    EXPECT_EQ(0, MkDirAll(child));
    EXPECT_EQ(0, stat(child.c_str(), &s));

    END_TEST;
}

bool WriteSummaryJSONSucceeds() {
    BEGIN_TEST;

    // A reasonable guess that the function won't output more than this.
    fbl::unique_ptr<char[]> buf(new char[kOneMegabyte]);
    FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
    const fbl::Vector<Result> results = {Result("/a", SUCCESS, 0),
                                         Result("b", FAILED_TO_LAUNCH, 0)};
    ASSERT_EQ(0, WriteSummaryJSON(results, kOutputFileName, "/tmp/file_path", buf_file));
    fclose(buf_file);
    // We don't have a JSON parser in zircon right now, so just hard-code the expected output.
    const char kExpectedJSONOutput[] = R"({"tests":[
{"name":"/a","output_file":"a/output.txt","result":"PASS"},
{"name":"b","output_file":"b/output.txt","result":"FAIL"}
],
"outputs": {
"syslog_file":"/tmp/file_path"
}}
)";
    EXPECT_STR_EQ(kExpectedJSONOutput, buf.get());

    END_TEST;
}

bool WriteSummaryJSONSucceedsWithoutSyslogPath() {
    BEGIN_TEST;

    // A reasonable guess that the function won't output more than this.
    fbl::unique_ptr<char[]> buf(new char[kOneMegabyte]);
    FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
    const fbl::Vector<Result> results = {Result("/a", SUCCESS, 0),
                                         Result("b", FAILED_TO_LAUNCH, 0)};
    ASSERT_EQ(0, WriteSummaryJSON(results, kOutputFileName, /*syslog_path=*/"", buf_file));
    fclose(buf_file);
    // With an empty syslog_path, we expect no values under "outputs" and "syslog_file" to
    // be generated in the JSON output.
    const char kExpectedJSONOutput[] = R"({"tests":[
{"name":"/a","output_file":"a/output.txt","result":"PASS"},
{"name":"b","output_file":"b/output.txt","result":"FAIL"}
]}
)";
    EXPECT_STR_EQ(kExpectedJSONOutput, buf.get(), );

    END_TEST;
}

bool WriteSummaryJSONBadTestName() {
    BEGIN_TEST;

    // A reasonable guess that the function won't output more than this.
    fbl::unique_ptr<char[]> buf(new char[kOneMegabyte]);
    FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
    // A test name and output file consisting entirely of slashes should trigger an error.
    const fbl::Vector<Result> results = {Result("///", SUCCESS, 0),
                                         Result("b", FAILED_TO_LAUNCH, 0)};
    ASSERT_NE(0, WriteSummaryJSON(results, /*output_file_basename=*/"///", /*syslog_path=*/"/", buf_file));
    fclose(buf_file);

    END_TEST;
}

bool ResolveGlobsNoMatches() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    fbl::Vector<fbl::String> resolved;
    const fbl::String test_fs_glob = JoinPath(test_dir.path(), "bar*");
    const char* globs[] = {"/foo/bar/*", test_fs_glob.c_str()};
    ASSERT_EQ(0, ResolveGlobs(globs, sizeof(globs) / sizeof(globs[0]), &resolved));
    EXPECT_EQ(0, resolved.size());

    END_TEST;
}

bool ResolveGlobsMultipleMatches() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String existing_dir_path =
        JoinPath(test_dir.path(), "existing-dir/prefix-suffix");
    const fbl::String existing_file_path =
        JoinPath(test_dir.path(), "existing-file");
    const fbl::String existing_dir_glob =
        JoinPath(test_dir.path(), "existing-dir/prefix*");
    const char* globs[] = {"/does/not/exist/*",
                           existing_dir_glob.c_str(), // matches existing_dir_path.
                           existing_file_path.c_str()};
    ASSERT_EQ(0, MkDirAll(existing_dir_path));
    const int existing_file_fd = open(existing_file_path.c_str(), O_CREAT);
    ASSERT_NE(-1, existing_file_fd, strerror(errno));
    ASSERT_NE(-1, close(existing_file_fd), strerror(errno));
    fbl::Vector<fbl::String> resolved;
    ASSERT_EQ(0, ResolveGlobs(globs, sizeof(globs) / sizeof(globs[0]), &resolved));
    ASSERT_EQ(2, resolved.size());
    EXPECT_STR_EQ(existing_dir_path.c_str(), resolved[0].c_str());

    END_TEST;
}

bool RunTestSuccess() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    fbl::String test_name = JoinPath(test_dir.path(), "succeed.sh");
    const char* argv[] = {test_name.c_str()};
    ScopedScriptFile script(argv[0], "exit 0");
    const Result result = PlatformRunTest(argv, 1, nullptr);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);

    END_TEST;
}

bool RunTestSuccessWithStdout() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    fbl::String test_name = JoinPath(test_dir.path(), "succeed.sh");
    const char* argv[] = {test_name.c_str()};
    const char expected_output[] = "Expect this!\n";
    // Produces expected_output, b/c echo adds newline
    const char script_contents[] = "echo Expect this!";
    ScopedScriptFile script(argv[0], script_contents);

    fbl::String output_filename = JoinPath(test_dir.path(), "test.out");
    const Result result = PlatformRunTest(argv, 1, output_filename.c_str());

    FILE* output_file = fopen(output_filename.c_str(), "r");
    ASSERT_TRUE(output_file);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
    fclose(output_file);
    EXPECT_STR_EQ(expected_output, buf);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(SUCCESS, result.launch_status);
    EXPECT_EQ(0, result.return_code);

    END_TEST;
}

bool RunTestFailureWithStderr() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    fbl::String test_name = JoinPath(test_dir.path(), "fail.sh");
    const char* argv[] = {test_name.c_str()};
    const char expected_output[] = "Expect this!\n";
    // Produces expected_output, b/c echo adds newline
    const char script_contents[] = "echo Expect this! 1>&2\nexit 77";
    ScopedScriptFile script(argv[0], script_contents);

    fbl::String output_filename = JoinPath(test_dir.path(), "test.out");
    const Result result = PlatformRunTest(argv, 1, output_filename.c_str());

    FILE* output_file = fopen(output_filename.c_str(), "r");
    ASSERT_TRUE(output_file);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
    fclose(output_file);
    EXPECT_STR_EQ(expected_output, buf);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result.launch_status);
    EXPECT_EQ(77, result.return_code);

    END_TEST;
}

bool RunTestFailureToLoadFile() {
    BEGIN_TEST;

    const char* argv[] = {"i/do/not/exist/", nullptr};

    const Result result = PlatformRunTest(argv, 1, nullptr);
    EXPECT_STR_EQ(argv[0], result.name.c_str());
    EXPECT_EQ(FAILED_TO_LAUNCH, result.launch_status);

    END_TEST;
}

bool RunTestsInDirBasic() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String succeed_file_name = JoinPath(test_dir.path(), "succeed.sh");
    ScopedScriptFile succeed_file(succeed_file_name, kEchoSuccessAndArgs);
    const fbl::String fail_file_name = JoinPath(test_dir.path(), "fail.sh");
    ScopedScriptFile fail_file(fail_file_name, kEchoFailureAndArgs);
    int num_failed;
    fbl::Vector<Result> results;
    const signed char verbosity = -1;
    EXPECT_FALSE(RunTestsInDir(PlatformRunTest, test_dir.path(), {}, nullptr,
                               nullptr, verbosity, &num_failed, &results));
    EXPECT_EQ(1, num_failed);
    EXPECT_EQ(2, results.size());
    bool found_succeed_result = false;
    bool found_fail_result = false;
    // The order of the results is not defined, so just check that each is present.
    for (const Result& result : results) {
        if (fbl::StringPiece(result.name) == succeed_file.path()) {
            found_succeed_result = true;
            EXPECT_EQ(SUCCESS, result.launch_status);
        } else if (fbl::StringPiece(result.name) == fail_file.path()) {
            found_fail_result = true;
            EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result.launch_status);
        }
    }
    EXPECT_TRUE(found_succeed_result);
    EXPECT_TRUE(found_fail_result);

    END_TEST;
}

bool RunTestsInDirFilter() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String succeed_file_name = JoinPath(test_dir.path(), "succeed.sh");
    ScopedScriptFile succeed_file(succeed_file_name, kEchoSuccessAndArgs);
    const fbl::String fail_file_name = JoinPath(test_dir.path(), "fail.sh");
    ScopedScriptFile fail_file(fail_file_name, kEchoFailureAndArgs);
    int num_failed;
    fbl::Vector<Result> results;
    fbl::Vector<fbl::String> filter_names({"succeed.sh"});
    const signed char verbosity = -1;
    EXPECT_TRUE(RunTestsInDir(PlatformRunTest, test_dir.path(), filter_names,
                              nullptr, nullptr, verbosity, &num_failed,
                              &results));
    EXPECT_EQ(0, num_failed);
    EXPECT_EQ(1, results.size());
    EXPECT_STR_EQ(results[0].name.c_str(), succeed_file.path().data());

    END_TEST;
}

bool RunTestsInDirWithVerbosity() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    const fbl::String succeed_file_name = JoinPath(test_dir.path(), "succeed.sh");
    ScopedScriptFile succeed_file(succeed_file_name, kEchoSuccessAndArgs);
    int num_failed;
    fbl::Vector<Result> results;
    const signed char verbosity = 77;
    const fbl::String output_dir = JoinPath(test_dir.path(), "output");
    const char output_file_base_name[] = "output.txt";
    ASSERT_EQ(0, MkDirAll(output_dir));
    EXPECT_TRUE(RunTestsInDir(PlatformRunTest, test_dir.path(), {},
                              output_dir.c_str(), output_file_base_name, verbosity,
                              &num_failed, &results));
    EXPECT_EQ(0, num_failed);
    EXPECT_EQ(1, results.size());

    fbl::String output_path = JoinPath(
        JoinPath(output_dir, succeed_file.path()), output_file_base_name);
    FILE* output_file = fopen(output_path.c_str(), "r");
    ASSERT_TRUE(output_file);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
    fclose(output_file);
    EXPECT_STR_EQ("Success! v=77\n", buf);

    END_TEST;
}

BEGIN_TEST_CASE(TestHelpers)
RUN_TEST(ScopedDirsAndFilesAreIndeedScoped)
END_TEST_CASE(TestHelpers)

BEGIN_TEST_CASE(ParseTestNames)
RUN_TEST(ParseTestNamesEmptyStr)
RUN_TEST(ParseTestNamesEmptyStrInMiddle)
RUN_TEST(ParseTestNamesNormal)
RUN_TEST(ParseTestNamesTrailingComma)
END_TEST_CASE(ParseTestNames)

BEGIN_TEST_CASE(IsInWhitelist)
RUN_TEST(EmptyWhitelist)
RUN_TEST(NonemptyWhitelist)
END_TEST_CASE(IsInWhitelist)

BEGIN_TEST_CASE(JoinPath)
RUN_TEST(JoinPathNoTrailingSlash)
RUN_TEST(JoinPathTrailingSlash)
RUN_TEST(JoinPathAbsoluteChild)
END_TEST_CASE(JoinPath)

BEGIN_TEST_CASE(MkDirAll)
RUN_TEST(MkDirAllTooLong)
RUN_TEST(MkDirAllAlreadyExists)
RUN_TEST(MkDirAllParentAlreadyExists)
RUN_TEST(MkDirAllParentDoesNotExist)
END_TEST_CASE(MkDirAll)

BEGIN_TEST_CASE(WriteSummaryJSON)
RUN_TEST(WriteSummaryJSONSucceeds)
RUN_TEST(WriteSummaryJSONSucceedsWithoutSyslogPath)
RUN_TEST(WriteSummaryJSONBadTestName)
END_TEST_CASE(WriteSummaryJSON)

BEGIN_TEST_CASE(ResolveGlobs)
RUN_TEST(ResolveGlobsNoMatches)
RUN_TEST(ResolveGlobsMultipleMatches)
END_TEST_CASE(ResolveGlobs)

BEGIN_TEST_CASE(RunTest)
RUN_TEST(RunTestSuccess)
RUN_TEST(RunTestSuccessWithStdout)
RUN_TEST(RunTestFailureWithStderr)
RUN_TEST(RunTestFailureToLoadFile)
END_TEST_CASE(RunTest)

BEGIN_TEST_CASE(RunTestsInDir)
RUN_TEST(RunTestsInDirBasic)
RUN_TEST(RunTestsInDirFilter)
RUN_TEST(RunTestsInDirWithVerbosity)
END_TEST_CASE(RunTestsInDir)

} // namespace
} // namespace runtests
