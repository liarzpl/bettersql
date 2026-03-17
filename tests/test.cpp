
#include "../BetterSQL/bettersql.hpp"
#include <cassert>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#define GREEN "\033[32m"
#define RED "\033[31m"
#define BOLD "\033[1m"
#define RESET "\033[0m"

class TestRunner
{
  int total = 0;
  int passed = 0;

public:
  void run(const std::string &name, bool (*test_func)())
  {
    total++;
    std::cout << "Testing " << std::left << std::setw(40) << name << "... ";
    if (test_func())
    {
      passed++;
      std::cout << GREEN << BOLD << "[PASSED]" << RESET << std::endl;
    }
    else
    {
      std::cout << RED << BOLD << "[FAILED]" << RESET << std::endl;
    }
  }
  void summary()
  {
    std::cout << "\n"
              << BOLD << "Test Summary: " << (passed == total ? GREEN : RED)
              << passed << "/" << total << " passed." << RESET << std::endl;
  }
};

bool test_connection()
{
  try
  {
    bsql::BetterSql db(":memory:");
    return true;
  }
  catch (...)
  {
    return false;
  }
}

bool test_crud_operations()
{
  bsql::BetterSql db(":memory:");
  db.execute("CREATE TABLE test (id INTEGER, val TEXT);");
  db.query("INSERT INTO test (id, val) VALUES (?, ?);", 1, "hello");

  auto res = db.query("SELECT val FROM test WHERE id = ?;", 1);
  return res.is_success && res.data[0][0].as<std::string>() == "hello";
}

bool test_transaction_rollback()
{
  bsql::BetterSql db(":memory:");
  db.execute("CREATE TABLE bank (balance REAL);");
  try
  {
    db.transaction([&]()
                   {
      db.query("INSERT INTO bank VALUES (?);", 100.0);
      throw std::runtime_error("Failure"); });
  }
  catch (...)
  {
  }

  auto res = db.query("SELECT COUNT(*) FROM bank;");
  return res.data[0][0].as<int64_t>() == 0;
}

bool test_null_handling()
{
  bsql::BetterSql db(":memory:");
  db.execute("CREATE TABLE n (v TEXT);");
  db.query("INSERT INTO n VALUES (?);", std::nullopt);

  auto res = db.query("SELECT v FROM n;");
  return res.data[0][0].is_null() &&
         !res.data[0][0].as_opt<std::string>().has_value();
}

bool test_blob_storage()
{
  bsql::BetterSql db(":memory:");
  db.execute("CREATE TABLE b (data BLOB);");
  std::vector<uint8_t> original = {0x1, 0x2, 0x3};
  db.query("INSERT INTO b VALUES (?);", original);

  auto res = db.query("SELECT data FROM b;");
  return res.data[0][0].as<std::vector<uint8_t>>() == original;
}

bool test_thread_safety()
{
  const char *mem_uri = "file:threadtest?mode=memory&cache=shared";

  bsql::BetterSql db(mem_uri);
  db.execute("CREATE TABLE t (v INTEGER);");

  auto worker = [&]()
  {
    bsql::BetterSql local_db(mem_uri);
    for (int i = 0; i < 100; ++i)
      local_db.query("INSERT INTO t VALUES (?);", 1);
  };

  std::vector<std::thread> pool;
  for (int i = 0; i < 5; ++i)
    pool.emplace_back(worker);
  for (auto &t : pool)
    t.join();

  auto res = db.query("SELECT COUNT(*) FROM t;");
  return res.data[0][0].as<int64_t>() == 500;
}

struct User
{
  int64_t id;
  std::string name;
};

namespace bsql
{
  template <>
  struct SqlMapper<User>
  {
    static User map(const std::vector<Value> &row)
    {
      return {row[0].as<int64_t>(), row[1].as<std::string>()};
    }
  };
};

bool test_struct_mapping()
{
  bsql::BetterSql db(":memory:");
  db.execute("CREATE TABLE u (id INTEGER, name TEXT);");
  db.query("INSERT INTO u VALUES (1, 'Alice');");
  db.use("u");
  auto users = db.select("*").get().as_struct<User>();
  return users.size() == 1 && users[0].name == "Alice";
}

int main()
{

  TestRunner runner;
  runner.run("Database Connection & WAL", test_connection);
  runner.run("Basic CRUD & Binding", test_crud_operations);
  runner.run("RAII Transaction Rollback", test_transaction_rollback);
  runner.run("NULL & std::optional Support", test_null_handling);
  runner.run("BLOB Binary Data Handling", test_blob_storage);
  runner.run("Multi-threaded WAL Concurrency", test_thread_safety);
  runner.run("Custom Struct Mapping (ORM)", test_struct_mapping);

  runner.summary();
  return 0;
}