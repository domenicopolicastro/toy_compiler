define double @add(double %a, double %b) {
entry:
  %b2 = alloca double, align 8
  %a1 = alloca double, align 8
  store double %a, ptr %a1, align 8
  store double %b, ptr %b2, align 8
  %a3 = load double, ptr %a1, align 8
  %b4 = load double, ptr %b2, align 8
  %addres = fadd double %a3, %b4
  ret double %addres
}

