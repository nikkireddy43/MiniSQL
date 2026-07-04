#include <gtest/gtest.h>
#include <cstdio>
#include "storage/Page.h"
#include "storage/DiskManager.h"

using namespace minisql;

// ---------- Page tests (pure in-memory, no disk involved) ----------

TEST(PageTest, EmptyPageHasZeroRecords) {
    Page page;
    EXPECT_EQ(page.numRecords(), 0);
    EXPECT_TRUE(page.getAllRecords().empty());
}

TEST(PageTest, SerializeThenDeserializeSingleIntRoundTrips) {
    Record record = {Value::makeInt(42)};
    std::vector<uint8_t> bytes;
    Page::serializeRecord(record, bytes);

    Record result;
    size_t consumed = Page::deserializeRecord(bytes.data(), result);

    EXPECT_EQ(consumed, bytes.size());
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].type, ValueType::INT);
    EXPECT_EQ(result[0].intVal, 42);
}

TEST(PageTest, SerializeThenDeserializeMixedRowRoundTrips) {
    // Matches the project's own example: student(id INT, name TEXT, cg FLOAT)
    Record record = {Value::makeInt(1), Value::makeText("Nikki"), Value::makeFloat(8.7)};
    std::vector<uint8_t> bytes;
    Page::serializeRecord(record, bytes);

    Record result;
    size_t consumed = Page::deserializeRecord(bytes.data(), result);

    EXPECT_EQ(consumed, bytes.size());
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0].intVal, 1);
    EXPECT_EQ(result[1].textVal, "Nikki");
    EXPECT_DOUBLE_EQ(result[2].floatVal, 8.7);
}

TEST(PageTest, SerializeThenDeserializeEmptyString) {
    Record record = {Value::makeText("")};
    std::vector<uint8_t> bytes;
    Page::serializeRecord(record, bytes);

    Record result;
    Page::deserializeRecord(bytes.data(), result);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].textVal, "");
}

TEST(PageTest, AppendSingleRecordThenReadBack) {
    Page page;
    Record record = {Value::makeInt(1), Value::makeText("Nikki"), Value::makeFloat(8.7)};

    ASSERT_TRUE(page.appendRecord(record));
    EXPECT_EQ(page.numRecords(), 1);

    auto records = page.getAllRecords();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0][0].intVal, 1);
    EXPECT_EQ(records[0][1].textVal, "Nikki");
    EXPECT_DOUBLE_EQ(records[0][2].floatVal, 8.7);
}

TEST(PageTest, AppendMultipleRecordsPreservesOrder) {
    Page page;
    page.appendRecord({Value::makeInt(1), Value::makeText("Nikki")});
    page.appendRecord({Value::makeInt(2), Value::makeText("Sam")});
    page.appendRecord({Value::makeInt(3), Value::makeText("Alex")});

    EXPECT_EQ(page.numRecords(), 3);
    auto records = page.getAllRecords();
    ASSERT_EQ(records.size(), 3);
    EXPECT_EQ(records[0][1].textVal, "Nikki");
    EXPECT_EQ(records[1][1].textVal, "Sam");
    EXPECT_EQ(records[2][1].textVal, "Alex");
}

TEST(PageTest, AppendFailsWhenPageIsFull) {
    Page page;
    // A long string to fill the page quickly.
    std::string bigText(500, 'x');
    int inserted = 0;
    while (page.appendRecord({Value::makeText(bigText)})) {
        inserted++;
        ASSERT_LT(inserted, 100) << "appendRecord never returned false - page-full check is missing";
    }
    // Page should be full now; numRecords should match how many succeeded.
    EXPECT_EQ(page.numRecords(), inserted);
}

// ---------- DiskManager tests (real file I/O against a temp file) ----------

class DiskManagerTest : public ::testing::Test {
protected:
    std::string testFile = "test_minisql_diskmanager.db";
    void TearDown() override {
        std::remove(testFile.c_str());
    }
};

TEST_F(DiskManagerTest, NewFileHasZeroPages) {
    DiskManager dm(testFile);
    EXPECT_EQ(dm.numPages(), 0);
}

TEST_F(DiskManagerTest, AllocatePageIncrementsCount) {
    DiskManager dm(testFile);
    int p0 = dm.allocatePage();
    int p1 = dm.allocatePage();
    EXPECT_EQ(p0, 0);
    EXPECT_EQ(p1, 1);
    EXPECT_EQ(dm.numPages(), 2);
}

TEST_F(DiskManagerTest, WriteThenReadPageRoundTrips) {
    DiskManager dm(testFile);
    dm.allocatePage();

    Page page;
    page.appendRecord({Value::makeInt(7), Value::makeText("hello")});
    dm.writePage(0, page);

    Page readBack;
    dm.readPage(0, readBack);

    auto records = readBack.getAllRecords();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0][0].intVal, 7);
    EXPECT_EQ(records[0][1].textVal, "hello");
}

TEST_F(DiskManagerTest, DataPersistsAcrossDiskManagerInstances) {
    {
        DiskManager dm(testFile);
        dm.allocatePage();
        Page page;
        page.appendRecord({Value::makeInt(99)});
        dm.writePage(0, page);
    }  // dm destructed here, file closed

    {
        DiskManager dm(testFile);
        EXPECT_EQ(dm.numPages(), 1);
        Page page;
        dm.readPage(0, page);
        auto records = page.getAllRecords();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0][0].intVal, 99);
    }
}
