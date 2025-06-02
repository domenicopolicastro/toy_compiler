# Kaleidoscope-like Compiler Project

## Description

This project is a compiler for a small, C-like language, inspired by the LLVM Kaleidoscope tutorial. It takes source code written in a custom language (with `.k` extension) and compiles it down to LLVM Intermediate Representation (IR), which can then be further compiled and executed.

The compiler includes a lexer (built with Flex), a parser (built with Bison), Abstract Syntax Tree (AST) generation, and LLVM IR code generation.

## Features

The language and compiler support:

* **Basic Arithmetic Operations**: `+`, `-`, `*`, `/`
* **Comparison Operators**: `<`, `==`
* **Logical Operators**: `and`, `or`, `not` (with short-circuiting for `and`/`or`)
* **Variables**:
    * Local variables with `var x = value;` or `var x;` (defaults to 0.0)
    * Global variables with `global G;` or `global G[size];` (defaults to 0.0 or array of zeros)
* **Control Flow**:
    * `if (cond) then_expr else else_expr` statements
    * `if (cond) then_expr` statements
    * `expr1 ? expr2 : expr3` conditional expressions (ternary operator)
    * `for` loops:
        * `for (var i = start; cond; step_expr) body_expr`
        * `for (init_expr; cond; step_expr) body_expr`
* **Functions**:
    * Function definition with `def fname(arg1, arg2) body_expr`
    * External function declaration with `extern fname(arg1, arg2)`
* **Unary Operators**:
    * Unary minus (`-expr`)
    * Logical not (`not expr`)
    * Pre-increment (`++var`)
    * Pre-decrement (`--var`)
* **Arrays**:
    * Global 1D arrays of doubles (e.g., `global A[10];`)
    * Array element access (e.g., `A[i]`)
    * Array element assignment (e.g., `A[i] = value;`)
* **Code Blocks**: ` { stmt1; stmt2; ...; return_expr }`
* **Semicolon-separated statements** at the top level and in blocks.

## Language Syntax Quick Overview


// Global variable declaration
global MyArray[20];
global AnotherGlobal;

// External function declaration
extern putchard(char);
extern printval(val controlchar); // controlchar: 0 for number, 1 for newline

// Function definition
def my_function(a, b) {
var result = a * b + (10 / a);
if (result < 100) {
printval(result, 0);
} else {
printval(999, 0);
};
result // return value of the block
}

// Main function (example entry point)
def main() {
var x = 10;
var y = 20;
MyArray[0] = x;
MyArray[1] = y;
printval(MyArray[0], 0);
printval(MyArray[1], 0);
my_function(x, y + MyArray[0]);
0 // main usually returns 0
}


## How to Build and Run

### Dependencies

* **Flex**: For lexical analysis.
* **Bison**: For parsing.
* **LLVM**: For IR generation and compilation (ensure `llvm-config` is in your PATH and development libraries are installed).
* **C++ Compiler**: A modern C++ compiler (e.g., g++, clang++).
* **Make**: For building the project using the provided Makefile (if applicable).

### Build Steps

1.  **Clone the repository**:
    ```bash
    git clone <your-repository-url>
    cd <your-project-directory>
    ```

2.  **Compile the compiler**:
    (Assuming you have a Makefile set up similar to typical Flex/Bison/LLVM projects)
    ```bash
    make
    ```
    This should generate an executable, e.g., `mycompiler`.

### Running the Compiler

1.  **Compile a `.k` source file to LLVM IR (`.ll` file)**:
    ```bash
    ./mycompiler your_source_file.k > output.ll
    ```
    Or, if your compiler writes to a file directly:
    ```bash
    ./mycompiler your_source_file.k -o output.ll
    ```

2.  **Compile LLVM IR to an executable using `llc` and `clang++` (or `g++`)**:
    * Generate object file from LLVM IR:
        ```bash
        llc -filetype=obj output.ll -o output.o
        ```
    * Link with a C++ compiler (and any external C functions if needed):
        ```bash
        clang++ output.o -o your_executable 
        # If you have external C functions (e.g., for printval, timek), link them:
        # clang++ output.o external_functions.c -o your_executable
        ```

3.  **Run the executable**:
    ```bash
    ./your_executable
    ```

## Project Structure

* `parser.y` (or similar): Bison grammar file defining the language syntax and AST construction rules.
* `lexer.l` (or similar): Flex file defining lexical tokens.
* `driver.hpp` / `driver.cpp`: Core compiler driver, manages parsing, AST, and code generation. Contains AST node class definitions and their `codegen()` methods.
* `main.cpp` (or similar): Entry point for the compiler executable, handles command-line arguments and invokes the driver.
* (Potentially a `Makefile` for build automation)

## Example Code (`inssort2.k`)

This example demonstrates array usage, loops, and function calls for an insertion sort algorithm.


extern randinit(seed);
extern randk();
extern timek();
extern printval(x controlchar); // controlchar=0 for num, 1 for newline

global A[10];

def inssort() {
for (var i=1; i<10; ++i) {
var pivot = A[i];
var j; // Declared without explicit init, defaults to 0.0
for (j = i-1; -1<j and pivot<A[j] ; --j)
A[j+1] = A[j];
A[j+1] = pivot
}
};

def main() {
var seed = timek();
randinit(seed);

printval(0, 2); // Assuming controlchar=2 for "Unsorted Array:" header
for (var i=0; i<10; ++i) {
A[i] = randk();
printval(A[i],0)
};
printval(0,1); // Newline

inssort();

printval(0, 3); // Assuming controlchar=3 for "Sorted Array:" header
for (var i=0; i<10; ++i)
printval(A[i],0)
;
printval(0,1); // Newline
0 // Return 0 from main
};

*(Note: You would need to provide C implementations for `randinit`, `randk`, `timek`, and `printval` and link them during the final executable creation step.)*

