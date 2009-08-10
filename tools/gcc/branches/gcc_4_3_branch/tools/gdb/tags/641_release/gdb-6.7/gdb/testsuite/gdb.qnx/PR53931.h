template<typename joe>
struct Class1 {
  joe a;
  Class1(int b) : a(b) {}
};
template<typename joe>
struct Class2 {
  joe a;
  Class2(const Class1<joe> &rhs) {
    a = 0;
    for(int i = 0; i < 10; ++i) {
      a += rhs.a;
    }
  }
};

