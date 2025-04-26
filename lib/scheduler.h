#pragma once
#include <vector>
#include <memory>
#include <cmath>  
#include <cassert>
#include <type_traits>
#include <utility>
#include <new>

template<typename T>
class future_result {
public:
    explicit future_result(int id = -1) : task_id_(id) {}
    int task_id() const { return task_id_; }
private:
    int task_id_;
};

template<typename T>
struct is_future_result : std::false_type {};

template<typename T>
struct is_future_result<future_result<T>> : std::true_type {};

template<typename T>
struct unwrap_future { using type = T; };

template<typename T>
struct unwrap_future<future_result<T>> { using type = T; };

class func_holder {
public:
    virtual ~func_holder() = default;
    virtual void call(void* result_ptr, void* arg1, void* arg2) = 0;
};

template<typename F, typename R>
class zero_arg_holder final : public func_holder {
public:
    explicit zero_arg_holder(F&& f) : func_(std::forward<F>(f)) {}
    void call(void* result_ptr, void*, void*) override {
        ::new (result_ptr) R(func_());
    }
private:
    F func_;
};

template<typename F, typename R, typename A1>
class one_arg_holder final : public func_holder {
public:
    explicit one_arg_holder(F&& f) : func_(std::forward<F>(f)) {}
    void call(void* result_ptr, void* arg1, void*) override {
        auto* p1 = static_cast<A1*>(arg1);
        ::new (result_ptr) R(func_(*p1));
    }
private:
    F func_;
};

template<typename F, typename R, typename A1, typename A2>
class two_arg_holder final : public func_holder {
public:
    explicit two_arg_holder(F&& f) : func_(std::forward<F>(f)) {}
    void call(void* result_ptr, void* arg1, void* arg2) override {
        auto* p1 = static_cast<A1*>(arg1);
        auto* p2 = static_cast<A2*>(arg2);
        ::new (result_ptr) R(func_(*p1, *p2));
    }
private:
    F func_;
};

class any_result {
public:
    alignas(std::max_align_t) char storage[64];
    bool is_set = false;

    template<typename T>
    T& ref() { return *reinterpret_cast<T*>(storage); }

    template<typename T>
    const T& cref() const { return *reinterpret_cast<const T*>(storage); }
};

class task {
public:
    std::unique_ptr<func_holder> holder;
    int  dep_id1   = -1;
    int  dep_id2   = -1;
    void* arg_ptr1 = nullptr;
    void* arg_ptr2 = nullptr;
    any_result result;
    bool computed = false;

    void compute(std::vector<task>& tasks) {
        if (computed) return;
        auto resolve = [&](int dep_id, void* raw) -> void* {
            if (dep_id >= 0) {
                tasks[dep_id].compute(tasks);
                return static_cast<void*>(tasks[dep_id].result.storage);
            }
            return raw;
        };
        void* p1 = resolve(dep_id1, arg_ptr1);
        void* p2 = resolve(dep_id2, arg_ptr2);
        holder->call(result.storage, p1, p2);
        result.is_set = true;
        computed = true;
    }
};

class TTaskScheduler {
public:
    using task_id = int;

    template<typename F,
             typename = std::enable_if_t<!std::is_member_function_pointer_v<std::decay_t<F>>>>
    task_id add(F&& f) {
        using R = decltype(std::declval<F>()());
        task t;
        t.holder.reset(new zero_arg_holder<F, R>(std::forward<F>(f)));
        tasks_.push_back(std::move(t));
        return static_cast<task_id>(tasks_.size()) - 1;
    }

    template<typename F, typename Arg1,
             typename = std::enable_if_t<!std::is_member_function_pointer_v<std::decay_t<F>>>>
    task_id add(F&& f, Arg1&& a1) {
        using Un1 = typename unwrap_future<std::decay_t<Arg1>>::type;
        using R   = decltype(std::declval<F>()(std::declval<Un1>()));
        task t;
        handle_arg(t.dep_id1, t.arg_ptr1, std::forward<Arg1>(a1));
        t.holder.reset(new one_arg_holder<F, R, Un1>(std::forward<F>(f)));
        tasks_.push_back(std::move(t));
        return static_cast<task_id>(tasks_.size()) - 1;
    }

    template<typename F, typename Arg1, typename Arg2,
             typename = std::enable_if_t<!std::is_member_function_pointer_v<std::decay_t<F>>>>
    task_id add(F&& f, Arg1&& a1, Arg2&& a2) {
        using Un1 = typename unwrap_future<std::decay_t<Arg1>>::type;
        using Un2 = typename unwrap_future<std::decay_t<Arg2>>::type;
        using R   = decltype(std::declval<F>()(std::declval<Un1>(), std::declval<Un2>()));
        task t;
        handle_arg(t.dep_id1, t.arg_ptr1, std::forward<Arg1>(a1));
        handle_arg(t.dep_id2, t.arg_ptr2, std::forward<Arg2>(a2));
        t.holder.reset(new two_arg_holder<F, R, Un1, Un2>(std::forward<F>(f)));
        tasks_.push_back(std::move(t));
        return static_cast<task_id>(tasks_.size()) - 1;
    }

    template<typename C, typename R, typename A1, typename Arg>
    task_id add(R (C::*method)(A1) const, const C& obj, Arg&& a) {
        auto w = [method, &obj](A1 x) -> R { return (obj.*method)(x); };
        return add(std::move(w), std::forward<Arg>(a));
    }

    template<typename C, typename R, typename A1, typename Arg>
    task_id add(R (C::*method)(A1), C& obj, Arg&& a) {
        auto w = [method, &obj](A1 x) -> R { return (obj.*method)(x); };
        return add(std::move(w), std::forward<Arg>(a));
    }

    template<typename T>
    future_result<T> getFutureResult(task_id id) { return future_result<T>(id); }

    template<typename T>
    T getResult(task_id id) {
        assert(id >= 0 && id < static_cast<int>(tasks_.size()));
        tasks_[id].compute(tasks_);
        return tasks_[id].result.ref<T>();
    }

    void executeAll() {
        for (auto& t : tasks_) t.compute(tasks_);
    }

private:
    template<typename Arg>
    void handle_arg(int& dep_id, void*& arg_ptr, Arg&& arg) {
        if constexpr (is_future_result<std::decay_t<Arg>>::value) {
            dep_id = arg.task_id();
        } else {
            int dep = get_dependency_id(arg);
            if (dep >= 0) {
                dep_id = dep;
            } else {
                using Raw = typename unwrap_future<std::decay_t<Arg>>::type;
                auto* copy = new Raw(std::forward<Arg>(arg));
                arg_ptr = copy;
                constants_.emplace_back(copy, deleter<Raw>);
            }
        }
    }

    template<typename A>
    static int get_dependency_id(const A&) { return -1; }

    template<typename T>
    static int get_dependency_id(const future_result<T>& f) { return f.task_id(); }

    template<typename T>
    static void deleter(void* p) { delete static_cast<T*>(p); }

    using const_ptr = std::unique_ptr<void, void(*)(void*)>;

    std::vector<const_ptr> constants_;
    std::vector<task> tasks_;
};
