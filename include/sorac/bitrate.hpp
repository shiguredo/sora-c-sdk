#ifndef SORAC_BITRATE_HPP_
#define SORAC_BITRATE_HPP_

#include <numeric>
#include <ratio>
#include <type_traits>

namespace sorac {

template <class Period>
class Bitrate {
 public:
  typedef Period period;

  Bitrate() : count_(0) {}
  explicit Bitrate(int64_t count) : count_(count) {}
  template <class Period2>
  Bitrate(Bitrate<Period2> other)
      : count_(bitrate_cast<Bitrate<Period>>(other).count()) {
    static_assert(std::ratio_divide<Period2, Period>::type::den == 1);
  }
  Bitrate(const Bitrate& other) : count_(other.count_) {}

  Bitrate& operator=(const Bitrate&) = default;
  template <class Period2>
  Bitrate& operator=(Bitrate<Period2> other) {
    *this = Bitrate(other);
    return *this;
  }

  template <class Period2>
  Bitrate& operator+=(const Bitrate<Period2>& rhs) {
    *this = *this + rhs;
    return *this;
  }

  template <class Period2>
  Bitrate& operator-=(const Bitrate<Period2>& rhs) {
    *this = *this - rhs;
    return *this;
  }

  int64_t count() const { return count_; }

 private:
  int64_t count_;
};

}  // namespace sorac

// common_type の特殊化
namespace std {
template <typename Period1, typename Period2>
struct common_type<sorac::Bitrate<Period1>, sorac::Bitrate<Period2>> {
  typedef sorac::Bitrate<
      ratio<gcd(Period1::num, Period2::num),
            Period1::den / gcd(Period1::den, Period2::den) * Period2::den>>
      type;
};
}  // namespace std

namespace sorac {

template <class ToBitrate, class Period>
ToBitrate bitrate_cast(Bitrate<Period> from) {
  typedef typename std::ratio_divide<Period, typename ToBitrate::period> ratio;
  return ToBitrate(from.count() * ratio::num / ratio::den);
}

template <class Period1, class Period2>
typename std::common_type<Bitrate<Period1>, Bitrate<Period2>>::type operator+(
    Bitrate<Period1> lhs,
    Bitrate<Period2> rhs) {
  typedef
      typename std::common_type<Bitrate<Period1>, Bitrate<Period2>>::type ct;
  return ct(ct(lhs).count() + ct(rhs).count());
}

template <class Period1, class Period2>
typename std::common_type<Bitrate<Period1>, Bitrate<Period2>>::type operator-(
    Bitrate<Period1> lhs,
    Bitrate<Period2> rhs) {
  typedef
      typename std::common_type<Bitrate<Period1>, Bitrate<Period2>>::type ct;
  return ct(ct(lhs).count() - ct(rhs).count());
}

template <class Period1>
Bitrate<Period1> operator*(Bitrate<Period1> lhs, int64_t rhs) {
  return Bitrate<Period1>(lhs.count() * rhs);
}

typedef Bitrate<std::ratio<1>> Bps;
typedef Bitrate<std::kilo> Kbps;

}  // namespace sorac

#endif
