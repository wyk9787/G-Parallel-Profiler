#include <stdio.h>

#define LARGE_NUM 300
#define FLOATING_NUM 3.12349876

double calc();
double calc2(size_t n);

// This function is just trying to be computationally intensive
// Do NOT try to interpret what it is doing cause I don't know either
double calc() {
  // It may overflow but I don't care
  double sum = 0;

  for (size_t i = 0; i < LARGE_NUM; i++) {
    for (size_t j = 0; j < LARGE_NUM; j++) {
      for (size_t k = 0; k < LARGE_NUM; k++) {
        sum += (i * j - calc2(k) * (double)k / (j + i + 1)) - LARGE_NUM * k;
      }
    }
  }

  return sum;
}

double calc2(size_t n) {
  double num = 1;
  for (size_t i = 0; i < n; i++) {
    num *= FLOATING_NUM;
  }
  return num;
}

int main() { printf("%lf\n", calc()); }

