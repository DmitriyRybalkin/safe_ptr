
/*
 * This is the example of safe_ptr object
 * It has been taken from <https://www.codeproject.com/Articles/1183379/We-make-any-object-thread-safe>
 * and slightly modified
 */
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <numeric>

using namespace std;

/* Execute around paradigm */
template <typename T, typename mutex_type = std::recursive_mutex>
class execute_around {

    std::shared_ptr<mutex_type> mtx;
    std::shared_ptr<T> p;

    void lock() const { mtx->lock(); }
    void unlock() const { mtx->unlock(); }

public:
    std::shared_ptr<T> get_p() const { return p; }
    std::shared_ptr<mutex_type> get_mtx() const { return mtx; }

public:
        class proxy {
                std::unique_lock<mutex_type> lock;
                T *const p;
            public:
                proxy(T * const _p, mutex_type &_mtx) : lock(_mtx), p(_p) { std::cout << "locked\n"; }
                proxy(proxy &&px) : lock(std::move(px.lock)), p(px.p) {}
                ~proxy() { std::cout << "unlocked\n"; }
                T* operator -> () { return p; }
                const T* operator -> () const { return p; }
        };

        template<typename ...Args>
        execute_around (Args ... args) :
            mtx(std::make_shared<mutex_type>()), p(std::make_shared<T>(args...)) {}

        proxy operator -> () { return proxy(p.get(), *mtx); }
        const proxy operator->() const { return proxy(p.get(), *mtx); }
        template<class... mutex_types> friend class std::lock_guard;
};

/* Thread-safe container for any type */
template <typename T, typename mutex_t = std::recursive_mutex,
                typename x_lock_t = std::unique_lock<mutex_t>,
                typename s_lock_t = std::unique_lock<mutex_t>>
class safe_ptr {
    typedef mutex_t mtx_t;
    const std::shared_ptr<T> ptr;
    std::shared_ptr<mutex_t> mtx_ptr;

    template<typename req_lock>
    class auto_lock_t {
        T * const ptr;
        req_lock lock;

        public:
            auto_lock_t(auto_lock_t &&o) : ptr(std::move(o.ptr)), lock(std::move(o.lock)) {}
            auto_lock_t(T * const _ptr, mutex_t &_mtx) : ptr(_ptr), lock(_mtx) {}
            T *operator->() { return ptr; }
            const T* operator->() const { return ptr; }
    };

    template<typename req_lock>
    class auto_lock_obj_t {
        T * const ptr;
        req_lock lock;

        public:
            auto_lock_obj_t(auto_lock_obj_t &&o):
                ptr(std::move(o.ptr)), lock(std::move(o.lock)) {}
            auto_lock_obj_t(T * const _ptr, mutex_t &_mtx) : ptr(_ptr), lock(_mtx) {}
            template<typename arg_t>
            auto operator[](arg_t arg) -> decltype((*ptr)[arg]) { return (*ptr)[arg]; }
    };

    void lock() { mtx_ptr->lock(); }
    void unlock() { mtx_ptr->unlock(); }
    friend struct link_safe_ptrs;
    template<class... mutex_types> friend class std::lock_guard;

public:
    template<typename... Args>
    safe_ptr(Args... args) : ptr(std::make_shared<T>(args...)),
                                    mtx_ptr(std::make_shared<mutex_t>()) {}
    auto_lock_t<x_lock_t> operator->() { return auto_lock_t<x_lock_t>(ptr.get(), *mtx_ptr); }
    auto_lock_obj_t<x_lock_t> operator* () { return auto_lock_obj_t<x_lock_t>(ptr.get(), *mtx_ptr); }
    const auto_lock_t<s_lock_t> operator-> () const { return auto_lock_t<s_lock_t>(ptr.get(), *mtx_ptr); }
    const auto_lock_obj_t<s_lock_t> operator* () const {return auto_lock_obj_t<s_lock_t>(ptr.get(), *mtx_ptr); }
};

safe_ptr<std::map<std::string, std::pair<std::string, int>>> safe_map_strings_global;

void func(decltype(safe_map_strings_global) safe_map_strings)
{
    (*safe_map_strings)["apple"].first = "fruit";
    (*safe_map_strings)["potato"].first = "vegetable";

    for(size_t i = 0; i < 10000; ++i) {
        safe_map_strings->at("apple").second++;
        safe_map_strings->find("potato")->second.second++;
    }

    auto const readonly_safe_map_string = safe_map_strings;

    std::cout << "potato is " << readonly_safe_map_string->at("potato").first <<
              " " << readonly_safe_map_string->at("potato").second <<
              ", apple is " << readonly_safe_map_string->at("apple").first <<
              " " << readonly_safe_map_string->at("apple").second << std::endl;
}

void test_execute_around() {
    typedef execute_around<std::vector<int>> T;
    T vecc(10, 10);

    int res = std::accumulate(vecc->begin(), vecc->end(), 0);

    std::cout << "1. execute_around::accumulate:res = " << res << std::endl;

    res = std::accumulate(
                (vecc.operator ->())->begin(),
                (vecc.operator ->())->end(),
                0);

    std::cout << "2. execute_around::accumulate:res = " << res << std::endl;

    res = std::accumulate(
                T::proxy(vecc.get_p().get(), *vecc.get_mtx())->begin(),
                T::proxy(vecc.get_p().get(), *vecc.get_mtx())->end(),
                0);

    std::cout << "3. execute_around::accumulate:res = " << res << std::endl;

    /* Further, according to standard - temporary proxy type objects
     * will be created before the function starts executing and will be
     * destroyed after end of the function (after the end of the entire expression)
     */
    T::proxy tmp1(vecc.get_p().get(), *vecc.get_mtx()); //Lock 1 std::recursive_mutex
    T::proxy tmp2(vecc.get_p().get(), *vecc.get_mtx()); //Lock 2 std::recursive_mutex

    res = std::accumulate(tmp1->begin(), tmp2->end(), 0);

    std::cout << "4. execute_around::accumulate:res = " << res << std::endl;

    tmp2.~proxy();  //unlock 2 std::recursive_mutex
    tmp1.~proxy();  //unlock 1 std::recursive_mutex
}

void test_safe_ptr() {
    std::vector<std::thread> vec_thread(10);

    for(auto &i : vec_thread) i = std::move(std::thread(func, safe_map_strings_global));
    for(auto &i : vec_thread) i.join();
}

int main()
{
    typedef void (*test_function)();
    std::unordered_map<std::string, test_function> test_table;

    test_table.insert(std::make_pair("test_execute_around", &test_execute_around));
    test_table.insert(std::make_pair("test_safe_ptr", &test_safe_ptr));

    int i(1);

    for(std::unordered_map<std::string, test_function>::const_iterator it = test_table.cbegin();
        it != test_table.cend();
        ++it) {

        std::cout << "=== Start test <" << it->first << "> #" << i << " ===" << std::endl;
        it->second();

        ++i;
    }

    std::cout << "end";
    int b; std::cin >> b;

    return 0;
}
