//
// Copyright (c) 2022 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace boost::sml::inline v_2_0_0 {
namespace mp {
template <int...>
struct index_sequence {
  using type = index_sequence;
};
#if __has_builtin(__make_integer_seq)
template <class T, T...>
struct integer_sequence;
template <int... Ns>
struct integer_sequence<int, Ns...> {
  using type = index_sequence<Ns...>;
};
template <int N>
struct make_index_sequence_impl {
  using type = typename __make_integer_seq<integer_sequence, int, N>::type;
};
#else
template <class, class>
struct concat;
template <int... I1, int... I2>
struct concat<index_sequence<I1...>, index_sequence<I2...>>
    : index_sequence<I1..., (sizeof...(I1) + I2)...> {};
template <int N>
struct make_index_sequence_impl
    : concat<typename make_index_sequence_impl<N / 2>::type,
             typename make_index_sequence_impl<N - N / 2>::type>::type {};
template <>
struct make_index_sequence_impl<0> : index_sequence<> {};
template <>
struct make_index_sequence_impl<1> : index_sequence<0> {};
#endif
template <int N>
using make_index_sequence = typename make_index_sequence_impl<N>::type;

template <auto N>
struct fixed_string {
  static constexpr auto size = N;

  constexpr fixed_string() = default;
  constexpr explicit(false) fixed_string(const char (&str)[N + 1]) {
    for (auto i = 0u; i < N; ++i) {
      data[i] = str[i];
      (hash ^= data[i]) <<= 1;
    }
  }

  constexpr auto operator*() const {
    fixed_string<N + 1> str{};
    str.data[0] = '*';
    for (auto i = 0u; i < N; ++i) {
      str.data[i + 1] = data[i];
      (str.hash ^= str.data[i + 1]) <<= 1;
    }
    return str;
  }

  char data[N + 1]{};
  unsigned hash{};
};

template <auto N>
fixed_string(const char (&str)[N]) -> fixed_string<N - 1>;

template <class T, auto N>
struct reader {
#if not defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#endif
  friend auto counted_flag(reader<T, N>);
#if not defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

template <class T, auto N>
struct setter {
  friend auto counted_flag(reader<T, N>) {}
  static constexpr unsigned value = N;
};

template <class T, auto Tag, unsigned NextVal = 0>
[[nodiscard]] consteval auto counter_impl() {
  if constexpr (requires(reader<T, NextVal> r) { counted_flag(r); }) {
    return counter_impl<T, Tag, NextVal + 1>();
  } else {
    return setter<T, NextVal>::value;
  }
}

template <class T, auto Tag = [] {}, auto N = counter_impl<T, Tag>()>
constexpr auto counter = N;

template <class... Ts>
struct inherit : Ts... {};

template <class T>
auto declval() -> T&&;
}  // namespace mp

namespace back {
template <class... Ts>
struct pool : Ts... {
  constexpr explicit(true) pool(Ts... ts) : Ts{ts}... {}
};

template <unsigned, class...>
struct transition {};

template <class>
class sm;
template <template <class...> class TList, class... Transitions>
class sm<TList<Transitions...>> {
  using mappings_t =
      mp::inherit<transition<mp::counter<typename Transitions::event>,
                             typename Transitions::event, Transitions>...>;

  static constexpr mappings_t mappings{};
  static constexpr auto num_of_regions =
      ((Transitions::src.data[0] == '*') + ...);

  static_assert(num_of_regions > 0, "At least one region is required!");

 public:
  constexpr explicit(true) sm(const TList<Transitions...>& transition_table)
      : transition_table_{transition_table} {
    const unsigned states[]{
        (Transitions::src.data[0] == '*' ? Transitions::src.hash : 0u)...};
    for (auto* current_state = &current_state_[0]; auto state : states) {
      if (state) {
        *current_state++ = state;
      }
    }
  }

  template<class TEvent>
  constexpr auto process_event(const TEvent& event) -> void {
    if constexpr (num_of_regions == 1u) {
      dispatch<TEvent>(event, current_state_[0], &mappings);
    } else {
      process_event(event, mp::make_index_sequence<num_of_regions>{});
    }
  }

 private:
  template <auto... Rs, class TEvent>
  constexpr auto process_event(const TEvent& event, mp::index_sequence<Rs...>)
      -> void {
    (dispatch<TEvent>(event, current_state_[Rs], &mappings), ...);
  }

  template <class TEvent, unsigned N = 0u, class T>
  constexpr auto dispatch(const TEvent& event, auto& current_state,
                          const transition<N, TEvent, T>*) -> void {
    if (T::src.hash != current_state or not static_cast<T&>(transition_table_)(event, current_state)) {
      dispatch<TEvent, N + 1u>(event, current_state, &mappings);
    }
  }

  template <class TEvent, unsigned = 0u>
  constexpr auto dispatch(const TEvent&, auto&, ...) -> void {}

  [[no_unique_address]] TList<Transitions...> transition_table_{};
  unsigned current_state_[num_of_regions]{};
};
}  // namespace back

namespace front {
namespace concepts {
struct invokable_base {
  void operator()();
};
template <class T>
struct invokable_impl : T, invokable_base {};
template <class T>
concept invokable = not requires { &invokable_impl<T>::operator(); };
}  // namespace concepts

constexpr auto invoke(const concepts::invokable auto& fn, const auto& event) {
  if constexpr (requires { fn(event); }) {
    return fn(event);
  } else {
    return fn();
  }
}

namespace detail {
constexpr auto none = [] {};
constexpr auto always = [] { return true; };
}  // namespace detail

template <mp::fixed_string Src = "", class TEvent = decltype(detail::none),
          class TGuard = decltype(detail::always),
          class TAction = decltype(detail::none), mp::fixed_string Dst = "">
struct transition {
  static constexpr auto src = Src;
  static constexpr auto dst = Dst;
  using event = TEvent;

  [[nodiscard]] constexpr auto operator*() const {
    return transition<*Src, TEvent, TGuard, TAction, Dst>{.guard = guard,
                                                          .action = action};
  }

  template <class T>
  [[nodiscard]] constexpr auto operator+(const T& t) const {
    return transition<Src, typename T::event, decltype(T::guard),
                      decltype(T::action)>{.guard = t.guard,
                                           .action = t.action};
  }

  template <class T>
  [[nodiscard]] constexpr auto operator[](const T& guard) const {
    return transition<Src, TEvent, T>{.guard = guard, .action = action};
  }

  template <class T>
  [[nodiscard]] constexpr auto operator/(const T& action) const {
    return transition<Src, TEvent, TGuard, T>{.guard = guard, .action = action};
  }

  template <class T>
  [[nodiscard]] constexpr auto operator=(const T&) const {
    return transition<src, TEvent, TGuard, TAction, T::src>{.guard = guard,
                                                            .action = action};
  }

  [[nodiscard]] constexpr auto operator()(const TEvent& event,
                                          [[maybe_unused]] auto& current_state)
      -> bool {
    if (invoke(guard, event)) {
      invoke(action, event);
      if constexpr (dst.size) {
        current_state = dst.hash;
      }
      return true;
    }
    return false;
  }

  [[no_unique_address]] TGuard guard;
  [[no_unique_address]] TAction action;
};

template <class TEvent>
constexpr auto event = transition<"", TEvent>{};
template <mp::fixed_string Str>
constexpr auto operator""_s() {
  return transition<Str>{};
}

[[nodiscard]] constexpr auto operator,(const concepts::invokable auto& lhs,
                                       const concepts::invokable auto& rhs) {
  return [=](const auto& event) {
    invoke(lhs, event);
    invoke(rhs, event);
  };
}
[[nodiscard]] constexpr auto operator and(const concepts::invokable auto& lhs,
                                          const concepts::invokable auto& rhs) {
  return [=](const auto& event) {
    return invoke(lhs, event) and invoke(rhs, event);
  };
}
[[nodiscard]] constexpr auto operator or(const concepts::invokable auto& lhs,
                                         const concepts::invokable auto& rhs) {
  return [=](const auto& event) {
    return invoke(lhs, event) or invoke(rhs, event);
  };
}
[[nodiscard]] constexpr auto operator not(const concepts::invokable auto& t) {
  return [=](const auto& event) { return not invoke(t, event); };
}
}  // namespace front

template <class T>
struct sm final : back::sm<decltype(mp::declval<T>()())> {
  constexpr explicit(false) sm(T t)
      : back::sm<decltype(mp::declval<T>()())>{t()} {}
};
template <class T>
sm(T&&) -> sm<T>;

namespace dsl {
template <class... Ts>
struct transition_table : back::pool<Ts...> {
  constexpr explicit(false) transition_table(Ts... ts)
      : back::pool<Ts...>{ts...} {}
};
using front::event;
using front::operator""_s;
using front::operator, ;
using front::operator not;
using front::operator and;
using front::operator or;
}  // namespace dsl
}  // namespace boost::sml::inline v_2_0_0