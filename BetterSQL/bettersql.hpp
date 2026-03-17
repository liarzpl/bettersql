/**
 * @file bettersql.hpp
 * @author liarzpl
 * @version 1.0.0
 */

#ifndef BETTERSQL_HPP
#define BETTERSQL_HPP

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace bsql
{

  using SqlValue = std::variant<std::monostate, int64_t, double, std::string,
                                std::vector<uint8_t>>;

  template <typename T>
  std::optional<T> get_if(const SqlValue &val)
  {
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t>)
    {
      if (std::holds_alternative<int64_t>(val))
        return static_cast<T>(std::get<int64_t>(val));
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
      if (std::holds_alternative<double>(val))
        return static_cast<T>(std::get<double>(val));
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
      if (std::holds_alternative<std::string>(val))
        return std::get<std::string>(val);
    }
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
    {
      if (std::holds_alternative<std::vector<uint8_t>>(val))
        return std::get<std::vector<uint8_t>>(val);
    }
    return std::nullopt;
  }

  struct Value
  {
    SqlValue v;

    Value() = default;

    template <typename T, typename = std::enable_if_t<
                              !std::is_same_v<std::decay_t<T>, Value>>>
    Value(T &&val) : v(std::forward<T>(val)) {}

    template <typename T>
    T as(const T &fallback = T()) const
    {
      auto opt = get_if<T>(v);
      return opt.value_or(fallback);
    }

    template <typename T>
    std::optional<T> as_opt() const { return get_if<T>(v); }

    bool is_null() const { return std::holds_alternative<std::monostate>(v); }
  };

  template <typename>
  struct is_optional : std::false_type
  {
  };
  template <typename T>
  struct is_optional<std::optional<T>> : std::true_type
  {
  };
  template <typename T>
  inline constexpr bool is_optional_v = is_optional<T>::value;

  template <typename T>
  struct SqlMapper
  {
  };
  class BetterSql;
  class Cursor;
  class QueryBuilder;

  struct alignas(16) SqlOutput
  {
    std::vector<std::vector<Value>> data;
    std::vector<std::string> column_names;
    std::string err_msg;
    int err_code = SQLITE_OK;
    bool is_success = true;

    SqlOutput() = default;
    explicit SqlOutput(bool success_val, int code_val, std::string msg_val)
        : err_msg(std::move(msg_val)), err_code(code_val),
          is_success(success_val) {}

    template <typename StructType>
    [[nodiscard]] std::vector<StructType> as_struct() const
    {
      std::vector<StructType> result;
      result.reserve(data.size());
      for (const auto &row : data)
      {
        result.push_back(SqlMapper<StructType>::map(row));
      }
      return result;
    }
  };

  class Cursor
  {
  public:
    explicit Cursor(sqlite3_stmt *stmt) noexcept
        : stmt_(stmt), is_eof_(!stmt),
          column_count_(stmt ? sqlite3_column_count(stmt) : 0) {}

    Cursor(int err_code, std::string err_msg) noexcept
        : stmt_(nullptr), is_eof_(true), column_count_(0), err_code_(err_code),
          err_msg_(std::move(err_msg)) {}

    ~Cursor()
    {
      if (stmt_)
        sqlite3_finalize(stmt_);
    }

    Cursor(const Cursor &) = delete;
    Cursor &operator=(const Cursor &) = delete;

    Cursor(Cursor &&other) noexcept
        : stmt_(other.stmt_), is_eof_(other.is_eof_),
          column_count_(other.column_count_), err_code_(other.err_code_),
          err_msg_(std::move(other.err_msg_))
    {
      other.stmt_ = nullptr;
    }

    Cursor &operator=(Cursor &&other) noexcept
    {
      if (this != &other)
      {
        if (stmt_)
          sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        is_eof_ = other.is_eof_;
        column_count_ = other.column_count_;
        err_code_ = other.err_code_;
        err_msg_ = std::move(other.err_msg_);
        other.stmt_ = nullptr;
      }
      return *this;
    }

    [[nodiscard]] bool next() noexcept
    {
      if (is_eof_ || !stmt_)
        return false;
      int rc = sqlite3_step(stmt_);
      if (rc == SQLITE_ROW)
        return true;
      is_eof_ = true;
      if (rc != SQLITE_DONE)
      {
        err_code_ = rc;
        err_msg_ = sqlite3_errstr(rc);
      }
      return false;
    }

    [[nodiscard]] bool is_valid() const noexcept { return stmt_ && !is_eof_; }
    [[nodiscard]] bool has_error() const noexcept
    {
      return err_code_ != SQLITE_OK && err_code_ != SQLITE_DONE;
    }
    [[nodiscard]] int error_code() const noexcept { return err_code_; }
    [[nodiscard]] std::string error_message() const { return err_msg_; }
    [[nodiscard]] int get_column_count() const noexcept { return column_count_; }
    sqlite3_stmt *get_raw_stmt() const noexcept { return stmt_; }

    [[nodiscard]] Value get_value(int col) const noexcept
    {
      if (!stmt_ || col < 0 || col >= column_count_)
        return std::monostate{};
      int type = sqlite3_column_type(stmt_, col);
      switch (type)
      {
      case SQLITE_INTEGER:
        return static_cast<int64_t>(sqlite3_column_int64(stmt_, col));
      case SQLITE_FLOAT:
        return sqlite3_column_double(stmt_, col);
      case SQLITE_TEXT:
      {
        const char *txt =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt_, col));
        int bytes = sqlite3_column_bytes(stmt_, col);
        return txt ? std::string(txt, bytes) : std::string("");
      }
      case SQLITE_BLOB:
      {
        const uint8_t *blob =
            static_cast<const uint8_t *>(sqlite3_column_blob(stmt_, col));
        int bytes = sqlite3_column_bytes(stmt_, col);
        return (blob && bytes > 0) ? std::vector<uint8_t>(blob, blob + bytes)
                                   : std::vector<uint8_t>();
      }
      default:
        return std::monostate{};
      }
    }

  private:
    sqlite3_stmt *stmt_ = nullptr;
    bool is_eof_ = false;
    int column_count_ = 0;
    int err_code_ = SQLITE_OK;
    std::string err_msg_;
  };

  class QueryBuilder
  {
    friend class BetterSql;

  public:
    QueryBuilder &where(std::string_view condition)
    {
      if (locked_)
        return *this;
      query_str_ += (has_where_ ? " AND " : " WHERE ");
      query_str_ += condition;
      has_where_ = true;
      return *this;
    }
    QueryBuilder &order_by(std::string_view columns)
    {
      locked_ = true;
      query_str_ += " ORDER BY ";
      query_str_ += columns;
      return *this;
    }
    QueryBuilder &limit(size_t n)
    {
      locked_ = true;
      query_str_ += " LIMIT " + std::to_string(n);
      return *this;
    }
    template <typename... Args>
    SqlOutput get(Args &&...args);
    template <typename... Args>
    Cursor get_lazy(Args &&...args);

  private:
    explicit QueryBuilder(BetterSql *db, std::string base_query)
        : db_ptr_(db), query_str_(std::move(base_query)) {}
    BetterSql *db_ptr_ = nullptr;
    std::string query_str_;
    bool has_where_ = false;
    bool locked_ = false;
  };

  class BetterSql
  {
  public:
    explicit BetterSql(std::string_view db_path)
    {
      if (sqlite3_open_v2(std::string(db_path).c_str(), &db_,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                              SQLITE_OPEN_NOMUTEX,
                          nullptr) != SQLITE_OK)
      {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_)
        {
          sqlite3_close(db_);
          db_ = nullptr;
        }
        throw std::runtime_error("[ERROR] Failed to open database: " + msg);
      }

      sqlite3_busy_timeout(db_, 5000);
      execute("PRAGMA journal_mode=WAL;");
      execute("PRAGMA synchronous=NORMAL;");
    }

    ~BetterSql()
    {
      if (db_)
      {
        sqlite3_exec(db_, "PRAGMA journal_mode=DELETE;", nullptr, nullptr,
                     nullptr);

        sqlite3_close(db_);
        db_ = nullptr;
      }
    }
    BetterSql(const BetterSql &) = delete;
    BetterSql &operator=(const BetterSql &) = delete;

    void use(std::string_view table_name) { current_table_ = table_name; }
    void execute(std::string_view sql)
    {
      std::string sql_str(sql);
      char *errmsg = nullptr;
      if (sqlite3_exec(db_, sql_str.c_str(), nullptr, nullptr, &errmsg) !=
          SQLITE_OK)
      {
        std::string msg = errmsg ? errmsg : sqlite3_errmsg(db_);
        if (errmsg)
          sqlite3_free(errmsg);
        throw std::runtime_error("[ERROR] Execution failed: " + msg);
      }
    }

    template <typename... Args>
    [[nodiscard]] SqlOutput query(std::string_view sql, Args &&...args)
    {
      return query_impl(sql, current_table_, std::forward<Args>(args)...);
    }

    template <typename... Args>
    [[nodiscard]] Cursor query_lazy(std::string_view sql, Args &&...args)
    {
      return cursor_impl(sql, current_table_, std::forward<Args>(args)...);
    }

    [[nodiscard]] QueryBuilder select(std::string_view columns)
    {
      if (current_table_.empty())
        throw std::runtime_error("[ERROR] No table selected.");
      return QueryBuilder(this, "SELECT " + std::string(columns) + " FROM " +
                                    current_table_);
    }

    class SqlTransaction
    {
    public:
      explicit SqlTransaction(BetterSql *db) : db_(db)
      {
        db_->execute("BEGIN TRANSACTION;");
      }
      ~SqlTransaction()
      {
        if (!finished_)
          try
          {
            db_->execute("ROLLBACK;");
          }
          catch (...)
          {
          }
      }
      void commit()
      {
        if (!finished_)
        {
          db_->execute("COMMIT;");
          finished_ = true;
        }
      }

      void rollback()
      {
        if (!finished_)
        {
          db_->execute("ROLLBACK;");
          finished_ = true;
        }
      }

    private:
      BetterSql *db_ = nullptr;
      bool finished_ = false;
    };

    template <typename Callable>
    void transaction(Callable &&func)
    {
      SqlTransaction tx(this);
      func();
      tx.commit();
    }

  private:
    sqlite3 *db_ = nullptr;
    std::string current_table_;

    sqlite3_stmt *prepare_statement(std::string_view sql_sv,
                                    std::string &err_msg_out,
                                    int &err_code_out) noexcept
    {
      sqlite3_stmt *stmt = nullptr;
      int rc = sqlite3_prepare_v2(
          db_, sql_sv.data(), static_cast<int>(sql_sv.size()), &stmt, nullptr);
      if (rc != SQLITE_OK)
      {
        err_msg_out = sqlite3_errmsg(db_);
        err_code_out = rc;
        if (stmt)
          sqlite3_finalize(stmt);
        return nullptr;
      }
      return stmt;
    }

    template <typename T>
    void bind_value(sqlite3_stmt *stmt, int index, const T &value)
    {
      using D = std::remove_cv_t<std::remove_reference_t<T>>;
      if constexpr (is_optional_v<D>)
      {
        if (!value.has_value())
          sqlite3_bind_null(stmt, index);
        else
          bind_value(stmt, index, value.value());
      }
      else if constexpr (std::is_same_v<D, std::nullopt_t>)
      {
        sqlite3_bind_null(stmt, index);
      }
      else if constexpr (std::is_integral_v<D>)
      {
        sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value));
      }
      else if constexpr (std::is_floating_point_v<D>)
      {
        sqlite3_bind_double(stmt, index, static_cast<double>(value));
      }
      else if constexpr (std::is_same_v<D, std::vector<uint8_t>>)
      {
        if (value.empty())
          sqlite3_bind_null(stmt, index);
        else
          sqlite3_bind_blob(stmt, index, value.data(),
                            static_cast<int>(value.size()), SQLITE_TRANSIENT);
      }
      else if constexpr (std::is_convertible_v<D, std::string_view>)
      {
        std::string_view sv(value);
        sqlite3_bind_text(stmt, index, sv.data(), static_cast<int>(sv.size()),
                          SQLITE_TRANSIENT);
      }
      else
      {
        sqlite3_bind_null(stmt, index);
      }
    }

    template <typename... Args>
    SqlOutput query_impl(std::string_view sql, std::string_view table_name,
                         Args &&...args)
    {
      std::string err_msg;
      int err_code = SQLITE_OK;
      sqlite3_stmt *stmt = prepare_statement(sql, err_msg, err_code);
      if (!stmt)
        return SqlOutput(false, err_code,
                         "[ERROR] Preparation failed: " + err_msg);
      Cursor cursor(stmt);

      try
      {
        int idx = 1;
        (bind_value(cursor.get_raw_stmt(), idx++, std::forward<Args>(args)), ...);
      }
      catch (const std::exception &e)
      {
        return SqlOutput(false, SQLITE_ERROR,
                         std::string("[ERROR] Bind exception: ") + e.what());
      }
      SqlOutput res(true, SQLITE_OK, {});
      const int ncols = cursor.get_column_count();
      res.column_names.reserve(ncols);
      for (int i = 0; i < ncols; ++i)
      {
        const char *name = sqlite3_column_name(cursor.get_raw_stmt(), i);
        res.column_names.emplace_back(name ? name : ("col_" + std::to_string(i)));
      }
      while (cursor.next())
      {
        std::vector<Value> row;
        row.reserve(ncols);
        for (int i = 0; i < ncols; ++i)
        {
          row.emplace_back(cursor.get_value(i)); // SqlValue
        }
        res.data.push_back(std::move(row));
      }
      if (cursor.has_error())
      {
        res.is_success = false;
        res.err_code = cursor.error_code();
        res.err_msg = "[ERROR] Step error: " + cursor.error_message();
      }
      return res;
    }

    template <typename... Args>
    Cursor cursor_impl(std::string_view sql, std::string_view table_name,
                       Args &&...args)
    {
      std::string err_msg;
      int err_code = SQLITE_OK;
      sqlite3_stmt *stmt = prepare_statement(sql, err_msg, err_code);
      if (!stmt)
        return Cursor(err_code, err_msg);
      Cursor cursor(stmt);
      try
      {
        int idx = 1;
        (bind_value(cursor.get_raw_stmt(), idx++, std::forward<Args>(args)), ...);
      }
      catch (const std::exception &e)
      {
        return Cursor(SQLITE_ERROR,
                      std::string("[ERROR] Bind exception: ") + e.what());
      }
      return cursor;
    }
  };

  template <typename... Args>
  inline SqlOutput QueryBuilder::get(Args &&...args)
  {
    return db_ptr_->query(query_str_ + ";", std::forward<Args>(args)...);
  }

  template <typename... Args>
  inline Cursor QueryBuilder::get_lazy(Args &&...args)
  {
    return db_ptr_->query_lazy(query_str_ + ";", std::forward<Args>(args)...);
  }
} // namespace bsql

#endif // BETTERSQL_HPP