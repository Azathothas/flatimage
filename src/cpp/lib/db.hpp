///
// @author      : Ruan E. Formigoni (ruanformigoni@gmail.com)
// @file        : db
///

#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <variant>

#include "log.hpp"
#include "../common.hpp"
#include "../macro.hpp"
#include "../std/enum.hpp"

namespace ns_db
{

namespace
{

namespace fs = std::filesystem;


using json_t = nlohmann::json;
using Exception = json_t::exception;

// class JsonIterator {{{
template<typename IteratorType>
class JsonIterator
{
  private:
    IteratorType m_it;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename IteratorType::value_type;
    using difference_type = typename IteratorType::difference_type;
    using pointer = typename IteratorType::pointer;
    using reference = typename IteratorType::reference;

    // Construct with an nlohmann::json iterator
    explicit JsonIterator(IteratorType it) : m_it(it) {}

    // Increment operators
    JsonIterator& operator++() { ++m_it; return *this; }
    JsonIterator operator++(int) { JsonIterator tmp = *this; ++(*this); return tmp; }

    // Dereference operators
    reference operator*() const { return *m_it; }
    pointer operator->() const { return &(*m_it); }

    // Comparison operators
    bool operator==(const JsonIterator& other) const { return m_it == other.m_it; }
    bool operator!=(const JsonIterator& other) const { return m_it != other.m_it; }
}; // }}}

} // anonymous namespace

// READ             : Reads existing database
// CREATE           : Creates new database, discards existing one
// UPDATE           : Updates existing database
// UPDATE_OR_CREATE : Updates existing database or creates a new one
ENUM(Mode, READ, CREATE, UPDATE, UPDATE_OR_CREATE);

// class Db {{{
class Db
{
  private:
    std::variant<json_t, std::reference_wrapper<json_t>> m_json;
    fs::path m_path_file_db;
    Mode m_mode;

    Db(std::reference_wrapper<json_t> json);

    json_t& data();
    json_t& data() const;
  public:
    // Iterators
    using iterator = JsonIterator<json_t::iterator>;
    using const_iterator = JsonIterator<json_t::const_iterator>;
    const_iterator cbegin() const { return const_iterator(data().cbegin()); }
    const_iterator cend() const { return const_iterator(data().cend()); }
    iterator begin() { return iterator(data().begin()); }
    iterator end() { return iterator(data().end()); }
    const_iterator begin() const { return const_iterator(data().cbegin()); }
    const_iterator end() const { return const_iterator(data().cend()); }

    // Constructors
    Db() = delete;
    Db(Db const&) = delete;
    Db(Db&&) = delete;
    Db(std::string_view json_data);
    Db(fs::path t, Mode mode);

    // Destructors
    ~Db();

    // Access
    bool is_array() const;
    bool is_object() const;
    bool is_string() const;
    decltype(auto) items() const;
    template<ns_concept::StringRepresentable T>
    bool contains(T&& t) const;
    bool empty() const;
    bool as_bool() const;
    template<typename T = std::string>
    std::set<T> as_set() const;
    template<typename T = std::string>
    std::vector<T> as_vector() const;
    std::string as_string() const;

    // Modifying
    template<ns_concept::StringRepresentable T>
    bool obj_erase(T&& t);
    template<ns_concept::Iterable T>
    requires ( not std::same_as<std::string, std::remove_cvref_t<T>> )
    bool obj_erase(T&& t);
    template<ns_concept::StringRepresentable T>
    bool set_erase(T&& t);
    template<ns_concept::Iterable T>
    requires ( not std::same_as<std::string, std::remove_cvref_t<T>> )
    bool set_erase(T&& t);
    template<std::predicate<std::string> F>
    void set_erase_if(F&& f);
    template<ns_concept::StringRepresentable T>
    Db& set_insert(T&& t);
    template<ns_concept::Iterable T>
    requires ( not std::same_as<std::string, std::remove_cvref_t<T>> )
    Db& set_insert(T&& t);

    // Operators
    operator std::string() const;
    operator json_t() const;
    Db operator=(Db const&) = delete;
    Db operator=(Db&&) = delete;
    template<ns_concept::StringRepresentable T>
    Db const& operator[](T&& t) const;
    template<ns_concept::StringRepresentable T>
    Db operator()(T&& t);
    template<typename T>
    requires ns_concept::StringRepresentable<T> or ns_concept::Iterable<T>
    decltype(auto) operator=(T&& t);
}; // class: Db }}}

// Constructors {{{
inline Db::Db(std::reference_wrapper<json_t> json)
{
  m_json = json;
} // Json

inline Db::Db(std::string_view json_data)
  : m_path_file_db("/dev/null")
  , m_mode(Mode::READ)
{
  // Validate contents
  ithrow_if(not json_t::accept(json_data), "Failed to parse json data: '{}'"_fmt(json_data));
  // Parse contents
  m_json = json_t::parse(json_data);
}

inline Db::Db(fs::path t, Mode mode)
  : m_path_file_db(t)
  , m_mode(mode)
{
  ns_log::debug()("Open file '{}' as '{}'", m_path_file_db, mode);

  auto f_parse_file = [](std::string const& name_file, std::ifstream const& f)
  {
    // Read to string
    std::string contents = ns_string::to_string(f.rdbuf());
    // Validate contents
    ithrow_if(not json_t::accept(contents), "Failed to parse db '{}'"_fmt(name_file));
    // Parse contents
    return json_t::parse(contents);
  };

  auto f_create = [&]
  {
    ns_log::debug()("Creating empty db file {}", t);
  };

  auto f_read = [&] -> bool
  {
    // Open target file as read
    std::ifstream file(t, std::ios::in);
    // Check for failure
    if ( not file.good() ) { return false; } // if
    // Try to parse
    m_json = f_parse_file(t.string(), file);
    // Close file
    file.close();
    return true;
  };

  switch(mode)
  {
    case Mode::READ:
    case Mode::UPDATE:
      ethrow_if(not f_read(), "Could not read file '{}'"_fmt(t));
      break;
    case Mode::CREATE:
      f_create();
      break;
    case Mode::UPDATE_OR_CREATE:
      if (not f_read()) { f_create(); }
      break;
  }
} // Db

// }}}

// Destructors {{{

inline Db::~Db()
{
  if ( m_mode != Mode::READ and std::holds_alternative<json_t>(m_json) )
  {
    std::ofstream file(m_path_file_db, std::ios::trunc);
    ereturn_if(! file.good(), "Failed to open '{}' for writing"_fmt(m_path_file_db));
    file << std::setw(2) << std::get<json_t>(m_json);
    file.close();
  } // if
} // Db

// }}}

// data() {{{
inline json_t& Db::data()
{
  if (std::holds_alternative<std::reference_wrapper<json_t>>(m_json))
  {
    return std::get<std::reference_wrapper<json_t>>(m_json).get();
  } // if

  return std::get<json_t>(m_json);
} // data() }}}

// data() const {{{
inline json_t& Db::data() const
{
  return const_cast<Db*>(this)->data();
} // data() const }}}

// is_array() {{{
inline bool Db::is_array() const
{
  return data().is_array();
} // is_array() }}}

// is_object() {{{
inline bool Db::is_object() const
{
  return data().is_object();
} // is_object() }}}

// is_string() {{{
inline bool Db::is_string() const
{
  return data().is_string();
} // is_string() }}}

// items() {{{
inline decltype(auto) Db::items() const
{
  return data().items();
} // items() }}}

// contains() {{{
template<ns_concept::StringRepresentable T>
bool Db::contains(T&& t) const
{
  return data().contains(t);
} // contains() }}}

// empty() {{{
inline bool Db::empty() const
{
  return data().empty();
} // empty() }}}

// as_bool() {{{
inline bool Db::as_bool() const
{
  json_t& json = data();
  ethrow_if(not json.is_boolean(), "Tried to access non-boolean as boolean in DB");
  return json;
} // as_bool() }}}

// as_set() {{{
template<typename T>
std::set<T> Db::as_set() const
{
  json_t& json = data();
  ethrow_if(not json.is_array(), "Tried to access non-array as array in DB");
  std::set<T> set;
  std::for_each(json.begin(), json.end(), [&](std::string e){ set.insert(T{e}); });
  return set;
} // as_set() }}}

// as_vector() {{{
template<typename T>
std::vector<T> Db::as_vector() const
{
  json_t& json = data();
  ethrow_if(not json.is_array(), "Tried to access non-array as array in DB");
  std::vector<T> vector;
  std::for_each(json.begin(), json.end(), [&](std::string e){ vector.push_back(T{e}); });
  return vector;
} // as_vector() }}}

// as_string() {{{
inline std::string Db::as_string() const
{
  return data().dump();
} // as_string() }}}

// obj_erase() {{{
template<ns_concept::StringRepresentable T>
bool Db::obj_erase(T&& t)
{
  std::string key = ns_string::to_string(t);
  json_t& json = data();
  ethrow_if(not json.is_object(), "Trying to erase a non-object entry");
  return json.erase(key) == 1;
} // obj_erase() }}}

// obj_erase() {{{
template<ns_concept::Iterable T>
requires ( not std::same_as<std::string, std::remove_cvref_t<T>> )
bool Db::obj_erase(T&& t)
{
  return std::ranges::all_of(t, [&]<typename E>(E&& e){ return erase(std::forward<E>(e)); });
} // obj_erase() }}}

// set_erase() {{{
template<ns_concept::StringRepresentable T>
bool Db::set_erase(T&& t)
{
  std::string key = ns_string::to_string(t);
  json_t& json = data();
  ethrow_if(not json.is_array(), "Trying to erase a non-array entry");
  auto it_search = std::find(json.begin(), json.end(), key);
  if ( it_search == json.end() ) { return false; }
  json.erase(std::distance(json.begin(), it_search));
  return true;
} // set_erase() }}}

// set_erase() {{{
template<ns_concept::Iterable T>
requires ( not std::same_as<std::string, std::remove_cvref_t<T>> )
bool Db::set_erase(T&& t)
{
  return std::ranges::all_of(t, [&]<typename E>(E&& e){ return this->set_erase(std::forward<E>(e)); });
} // set_erase() }}}

// set_erase_if() {{{
template<std::predicate<std::string> F>
void Db::set_erase_if(F&& f)
{
  json_t& json = data();
  ethrow_if(not json.is_array(), "Trying to erase a non-array entry");
  json.erase(std::remove_if(json.begin(), json.end(), f), json.end());
} // set_erase_if() }}}

// set_insert() {{{
template<ns_concept::StringRepresentable T>
Db& Db::set_insert(T&& t)
{
  auto& json = data();
  std::string key = ns_string::to_string(t);
  if ( std::find_if(json.cbegin()
    , json.cend()
    , [&](auto&& e){ return std::string{e} == key; }) == json.cend() )
  {
    json.push_back(key);
  } // if
  return *this;
} // set_insert() }}}

// set_insert() {{{
template<ns_concept::Iterable T>
requires ( not std::same_as<std::string, std::remove_cvref_t<T>> )
Db& Db::set_insert(T&& t)
{
  std::for_each(t.cbegin(), t.cend(), [&](auto&& e){ set_insert(e); });
  return *this;
} // set_insert() }}}

// operator::string() {{{
inline Db::operator std::string() const
{
  return data();
} // operator::string() }}}

// operator::json_t() {{{
inline Db::operator json_t() const
{
  return data();
} // operator::fs::path() }}}

// operator[] {{{
// Key exists and is accessed
template<ns_concept::StringRepresentable T>
Db const& Db::operator[](T&& t) const
{
  std::string key = ns_string::to_string(t);
  static std::unique_ptr<Db> db;

  json_t& json = data();

  // Check if key is present
  if ( ! json.contains(key) )
  {
    "Key '{}' not present in db file"_throw(key);
  } // if

  // Access key
  try
  {
    db = std::unique_ptr<Db>(new Db{std::reference_wrapper<json_t>(json[key])});
  } // try
  catch(std::exception const& e)
  {
    "Failed to parse key '{}': {}"_throw(key, e.what());
  } // catch

  // Unreachable, used to suppress no return warning
  return *db;
} // operator[] }}}

// operator() {{{
// Key exists or is created, and is accessed
template<ns_concept::StringRepresentable T>
Db Db::operator()(T&& t)
{
  std::string key = ns_string::to_string(t);
  json_t& json = data();

  // Access key
  try
  {
    return Db{std::reference_wrapper<json_t>(json[key])};
  } // try
  catch(std::exception const& e)
  {
    "Failed to parse key '{}': {}"_throw(key, e.what());
  } // catch

  // Unreachable, used to suppress no return warning
  return Db{std::reference_wrapper<json_t>(json[key])};
} // operator() }}}

// operator=(ns_concept::StringRepresentable) {{{
template<typename T>
requires ns_concept::StringRepresentable<T> or ns_concept::Iterable<T>
decltype(auto) Db::operator=(T&& t)
{
  json_t& json = data();
  json = t;
  return json;
} // operator=(ns_concept::StringRepresentable) }}}

// from_file() {{{
template<ns_concept::StringRepresentable T, typename F>
void from_file(T&& t, F&& f, Mode mode)
{
  // Create DB
  Db db = Db(std::forward<T>(t), mode);
  // Access
  f(db);
} // function: from_file }}}

// to_file() {{{
template<ns_concept::StringRepresentable T>
void to_file(Db const& json, T&& t)
{
  std::ofstream ofile_json{t};
  ofile_json << std::setw(2) << std::string{json};
  ofile_json.close();
} // function: to_file }}}

// query() {{{
template<typename F, typename... Args>
inline std::string query(F&& file, Args... args)
{
  std::string ret;

  auto f_access_impl = [&]<typename T, typename U>(T& ref_db, U&& u)
  {
    ref_db = std::reference_wrapper(ref_db.get()[u]);
  }; // f_access

  auto f_access = [&]<typename T, typename... U>(T& ref_db, U&&... u)
  {
    ( f_access_impl(ref_db, std::forward<U>(u)), ... );
  }; // f_access

  from_file(file, [&]<typename T>(T&& db)
  {
    // Get a ref to db
    auto ref_db = std::reference_wrapper<Db const>(db);

    // Update the ref to the selected query object
    f_access(ref_db, std::forward<Args>(args)...);

    // Assign result
    ret = ref_db.get();
  }, Mode::READ);

  return ret;
} // query() }}}

} // namespace ns_db

/* vim: set expandtab fdm=marker ts=2 sw=2 tw=100 et :*/
