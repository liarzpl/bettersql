# BetterSql

> **A modern, header-only C++17 wrapper for SQLite — RAII-safe, lightweight, and built around templates, `std::variant`, and `std::optional`.**

[![C++](https://img.shields.io/badge/C%2B%2B-17-orange)](https://en.cppreference.com/w/cpp/17)
[![SQLite](https://img.shields.io/badge/SQLite-3-003B57?logo=sqlite&logoColor=white)](https://www.sqlite.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

BetterSql is a **single-header** C++ wrapper around the SQLite C API. You still write SQL, but you don’t write SQLite plumbing.

## Contents

- [Why BetterSql](#why-bettersql)
- [Key features](#key-features)
- [Installation](#installation)
- [Detailed API reference & examples](#detailed-api-reference--examples)
  - [Basic setup](#basic-setup)
  - [Query builder](#query-builder)
  - [Data retrieval (`SqlOutput`, `Value`)](#data-retrieval-sqloutput-value)
  - [Struct mapping (`SqlMapper`, `as_struct<T>()`)](#struct-mapping-sqlmapper-as_structt)
  - [Lazy loading (`Cursor`, `query_lazy()`)](#lazy-loading-cursor-query_lazy)
  - [Transactions](#transactions)
  - [Automated binding (no `sqlite3_bind_*` calls)](#automated-binding-no-sqlite3_bind_-calls)
- [Design choices](#design-choices)
- [Contibution](#contribution)
- [License](#license)

# Why BetterSql?

Integrating SQLite's C API into modern C++ projects often introduces a significant impedance mismatch. The native API relies on manual resource management, lacks inherent type safety, and necessitates extensive boilerplate. BetterSql is engineered to resolve these pain points through a high-level, RAII-compliant abstraction layer.

| Feature           | SQLite C API                                | BetterSql                               |
| ----------------- | ------------------------------------------- | --------------------------------------- |
| Memory Management | Manual `sqlite3 *finalize`, `sqlite3_close` | RAII (Automatic cleanup)                |
| Type Safety       | Type-punned pointers & manual casting       | `std::variant` & template-based casting |
| Binding           | Manual index tracking `sqlite3_bind**`      | Variadic Templates (Automated binding)  |
| Error Handling    | Integer codes (easy to ignore)              | Exceptions & structured SqlOutput       |
| Querying          | Verbose boilerplate                         | Fluent Query Builder                    |

**Design Philosophy**

- **Memory Safety:** Every database handle and prepared statement is encapsulated within move-only RAII types, ensuring zero leaks even in exception-heavy paths.

- **Modern Semantics:** Leverages C++17 primitives to replace traditional C-style pointer manipulation with predictable, type-safe operations.

## Key features

- **Header-only / lightweight**: just include one header and link SQLite3.
- **Modern C++17**: templates, `std::variant`, `std::optional`, move-only RAII types.
- **RAII by default**:
- `BetterSql` owns the `sqlite3*` handle and closes it in the destructor.
- `Cursor` owns the `sqlite3_stmt*` and finalizes it in the destructor.
- `BetterSql::SqlTransaction` rolls back on scope exit unless you `commit()`.
- **Fluent `QueryBuilder`** for `SELECT`:
- `.where()` is chainable (subsequent calls append `AND`).
- `.order_by()` / `.limit()` lock the builder to prevent accidental `.where()` after those clauses.
- **Automated binding**:
- `int` / `int64_t`, floating-point, `std::string` / `std::string_view` / `const char*`
- `std::optional<T>` and `std::nullopt`
- `std::vector<uint8_t>` for BLOBs
- **Two result modes**:
- `SqlOutput` materializes results into memory.
- `Cursor` streams rows (memory-efficient).

## Installation

BetterSql is **header-only**.

**Prerequisites**

To use BetterSql, ensure your environment meets the following requirements:

- **Compiler:** A C++17 or higher version compliant compiler (GCC 7+, Clang 5+, or MSVC 2017+).

- **Dependency:** SQLite3 development files must be installed on your system.

- **Ubuntu/Debian:** `sudo apt-get install libsqlite3-dev`

- **macOS:** `brew install sqlite`

- **Windows:** `vcpkg install sqlite3` or download the precompiled binaries

#### Integration Methods

**Git Submodule**

The most robust way to manage dependencies is by adding BetterSql as a git submodule to your project:

- `git submodule add https://github.com/liarzpl/bettersql.git external/bettersql`

**Usage**

- **Add the header**: copy `bettersql.hpp` into your include path and:

```cpp
#include "bettersql.hpp"
```

Link SQLite3 (example):

```bash
g++ -std=c++17 main.cpp -lsqlite3 -o app
```

Using CMake (example):

```cmake
# Find SQLite3 package
find_package(SQLite3 REQUIRED)

# Add your executable
add_executable(my_app main.cpp)

# Include BetterSql path and link SQLite3
target_include_directories(my_app PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(my_app PRIVATE SQLite::SQLite3)
```

## Detailed API reference & examples

This section is written to match the actual public API in `bettersql.hpp`.

### Basic setup

Open a database, create a table, and select the active table with `use()`:

```cpp
#include "bettersql.hpp"

int main() {
    bsql::BetterSql db("app.db"); // opens/creates; throws std::runtime_error on failure

    db.execute(R"sql(
        CREATE TABLE IF NOT EXISTS users (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age  INTEGER
        );
    )sql");

    db.use("users"); // selects the table for QueryBuilder::select()
}
```

### Query builder

Build `SELECT` queries fluently and bind values via `.get(...)`:

```cpp
#include "bettersql.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main() {
    bsql::BetterSql db("app.db");
    db.use("users");

    auto out = db.select("id, name, age")
                .where("age >= ?")
                .where("name LIKE ?")
                .order_by("id DESC")
                .limit(10)
                .get(18, std::string("%a%"));

    if (!out.is_success) {
        std::cerr << "SQLite error [" << out.err_code << "]: " << out.err_msg << "\n";
        return 1;
    }

    for (const auto& row : out.data) {
        const auto id   = row[0].as<int64_t>(-1);
        const auto name = row[1].as<std::string>("<unknown>");
        const auto age  = row[2].as_opt<int>(); // NULL-aware

        std::cout << id << " | " << name << " | "
                << (age ? std::to_string(*age) : "NULL") << "\n";
    }
}
```

### Data retrieval (`SqlOutput`, `Value`)

`BetterSql::query(...)` and `QueryBuilder::get(...)` return a `SqlOutput`:

- **`SqlOutput::data`**: `std::vector<std::vector<Value>>` (rows × columns)
- **`SqlOutput::column_names`**: column names from SQLite
- **`SqlOutput::is_success`** / **`err_code`** / **`err_msg`**: basic error reporting

Each cell is a `Value`, backed by:

- `using SqlValue = std::variant<std::monostate, int64_t, double, std::string, std::vector<uint8_t>>;`

Common reads:

```cpp
// `Value` helpers:
// - as<T>(fallback): returns T or fallback if types mismatch / NULL
// - as_opt<T>(): returns std::optional<T> (empty on mismatch / NULL)
// - is_null(): true if SQL NULL

const bsql::Value& v = out.data[0][0];

auto i64  = v.as<int64_t>(0);
auto dbl  = v.as<double>(0.0);
auto text = v.as<std::string>("");

auto maybe_int = v.as_opt<int>();
if (v.is_null()) {
    // ...
}
```

### Struct mapping (`SqlMapper`, `as_struct<T>()`)

BetterSql provides a small mapping hook rather than a full ORM. To map rows into a struct, specialize `SqlMapper<T>` and call `SqlOutput::as_struct<T>()`.

```cpp
#include "bettersql.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct User {
    int64_t     id;
    std::string name;
};

namespace bsql {
    template <>
    struct SqlMapper<User> {
        static User map(const std::vector<Value>& row) {
            return {
                row[0].as<int64_t>(-1),
                row[1].as<std::string>("<unknown>")
            };
        }
    };
};

int main() {
    bsql::BetterSql db("app.db");
    db.use("users");

    auto out = db.select("id, name").order_by("id ASC").get();
    auto users = out.as_struct<User>();
}
```

### Lazy loading (`Cursor`, `query_lazy()`)

Use `Cursor` to stream results without materializing `SqlOutput::data` (good for large datasets).

`Cursor` is **move-only** and owns the prepared statement (finalized in `~Cursor()`).

```cpp
#include "bettersql.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main() {
    bsql::BetterSql db("app.db");

    // Stream rows:
    bsql::Cursor cur = db.query_lazy(
        "SELECT id, name, age FROM users WHERE id >= ? ORDER BY id ASC;",
        100
    );

    while (cur.next()) {
        bsql::Value id   = cur.get_value(0);
        bsql::Value name = cur.get_value(1);
        bsql::Value age  = cur.get_value(2);
        auto age_opt = age.as_opt<int>();

        std::cout << id.as<int64_t>(-1) << " | "
                << name.as<std::string>("<unknown>") << " | "
                << (age_opt ? std::to_string(*age_opt) : std::string("NULL"))
                << "\n";
    }

    if (cur.has_error()) {
        std::cerr << "Cursor error [" << cur.error_code() << "]: " << cur.error_message() << "\n";
        return 1;
    }
}
```

You can also stream via the query builder:

```cpp
bsql::BetterSql db("app.db");
db.use("users");

bsql::Cursor cur = db.select("id, name").where("id >= ?").order_by("id ASC").get_lazy(100);
while (cur.next()) {
    auto id = cur.get_value(0).as<int64_t>(-1);
    auto name = cur.get_value(1).as<std::string>("");
}
```

### Transactions

#### Scope-based RAII (`BetterSql::SqlTransaction`)

Rolls back automatically unless you call `commit()`.

```cpp
#include "bettersql.hpp"

#include <optional>
#include <string>

int main() {
    bsql::BetterSql db("app.db");

    bsql::BetterSql::SqlTransaction tx(&db);
    db.query("INSERT INTO users (name, age) VALUES (?, ?);", "Alice", 30);
    db.query("INSERT INTO users (name, age) VALUES (?, ?);", "Bob", std::nullopt);
    tx.commit();
}
```

#### Lambda helper (`db.transaction(...)`)

Commits if the lambda returns normally; rolls back if it throws.

```cpp
#include "bettersql.hpp"

#include <string>

int main() {
    bsql::BetterSql db("app.db");

    try {
        db.transaction([&]() {
            db.query("UPDATE users SET age = age + 1 WHERE name = ?;", "Alice");
            db.query("UPDATE users SET age = age + 1 WHERE name = ?;", "Bob");
        });
    } catch (const std::exception& e) {
        // Transaction was rolled back by RAII on unwind.
    }
}
```

### Automated binding (no `sqlite3_bind_*` calls)

BetterSql binds values using templates; you pass C++ values directly.

```cpp
#include "bettersql.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

int main() {
    bsql::BetterSql db("app.db");

    const int64_t user_id = 42;
    const std::string name = "Ada";
    const std::optional<int> age = std::nullopt; // binds NULL
    const std::vector<uint8_t> avatar = {0x89, 0x50, 0x4E, 0x47}; // binds BLOB

    db.execute("CREATE TABLE IF NOT EXISTS users (id INTEGER, name TEXT, age INTEGER, avatar BLOB);");

    // No manual sqlite3_bind_* calls:
    db.query("INSERT INTO users (id, name, age, avatar) VALUES (?, ?, ?, ?);",
            user_id, name, age, avatar);

    auto out = db.query("SELECT id, name, age, avatar FROM users WHERE id = ?;", user_id);
    if (!out.is_success) return 1;

    const auto& row = out.data[0];
    auto id_out = row[0].as<int64_t>(-1);
    auto name_out = row[1].as<std::string>("");
    auto age_out = row[2].as_opt<int>();
    auto avatar_out = row[3].as<std::vector<uint8_t>>({});
}
```

## Design choices

- **`std::variant` for type safety**: SQLite is dynamically typed, but `Value` exposes a small, explicit set of C++ types (`int64_t`, `double`, `std::string`, `std::vector<uint8_t>`, and NULL). This keeps reads predictable and avoids “stringly-typed” result handling.
- **Custom `SqlOutput`**: results are returned as a simple container (`data`, `column_names`, and error fields) to stay lightweight and easy to integrate into existing codebases.

## Contribution

Thank you for your interest in contributing to **BetterSql**! All contributions are welcome. See CONTRIBUTING.md for more info.

## License

By contributing, you agree that your contributions will be licensed under the project's [License](LICENSE).
