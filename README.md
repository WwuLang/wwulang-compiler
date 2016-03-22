# Simple Calculator Compiler Frontend with Spirit and LLVM
We wrote a compiler frontend that could take in simple programs consisting of
+, -, \*, and / with parethesis, assignment statements, and multiple lines
separated by semicolons. For the parser we used Boost Spirit, and then we
generated an intermediate representation using LLVM.

## Example
    $ make
    ...

    $ ./compiler 
    WwuLang Compiler
    > 3*5
    AST: 3 5 *
    Compiled: 

    define double @main() {
    entry:
      %multmp = fmul float 3.000000e+00, 5.000000e+00

    entry1:                                           ; No predecessors!
      ret float %multmp
    }

    > a=5;b=6;a*b
    AST: 5 =a 6 =b a b *
    Compiled: 

    define double @main() {
    entry:
      %b = alloca double
      %a = alloca double
      store float 5.000000e+00, double* %a
      store float 6.000000e+00, double* %b
      %a1 = load double, double* %a
      %b2 = load double, double* %b
      %multmp = fmul double %a1, %b2

    entry3:                                           ; No predecessors!
      ret double %multmp
    }
