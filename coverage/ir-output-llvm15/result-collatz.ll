; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private constant [7 x i8] c"%d -> \00"
@.str.1 = private constant [4 x i8] c"%d\0A\00"

define i32 @main() {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %0 = call i32 @"_Ym.collatz(I32)I32"(i32 7)
  ret i32 0
}

define internal i32 @"_Ym.collatz(I32)I32"(i32 %arg.int) {
decl:
  %k = alloca i32, align 4
  br label %entry

entry:                                            ; preds = %decl
  %int = alloca i32, align 4
  store i32 %arg.int, ptr %int, align 4
  store i32 0, ptr %k, align 4
  br label %while.test

while.test:                                       ; preds = %if.cont, %entry
  %0 = load i32, ptr %int, align 4
  %1 = icmp sgt i32 %0, 1
  br i1 %1, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %2 = load i32, ptr %int, align 4
  %3 = call i32 (ptr, ...) @printf(ptr @.str, i32 %2)
  br label %if.test

while.merge:                                      ; preds = %while.test
  %4 = load i32, ptr %int, align 4
  %5 = call i32 (ptr, ...) @printf(ptr @.str.1, i32 %4)
  %6 = load i32, ptr %k, align 4
  ret i32 %6

if.test:                                          ; preds = %while.head
  %7 = load i32, ptr %int, align 4
  %8 = srem i32 %7, 2
  %9 = icmp eq i32 %8, 0
  br i1 %9, label %if.then, label %if.else

if.then:                                          ; preds = %if.test
  %10 = load i32, ptr %int, align 4
  %11 = sdiv i32 %10, 2
  store i32 %11, ptr %int, align 4
  br label %if.cont

if.else:                                          ; preds = %if.test
  %12 = load i32, ptr %int, align 4
  %13 = mul i32 %12, 3
  %14 = add i32 %13, 1
  store i32 %14, ptr %int, align 4
  br label %if.cont

if.cont:                                          ; preds = %if.else, %if.then
  %ic.deref = load i32, ptr %k, align 4
  %15 = add i32 %ic.deref, 1
  store i32 %15, ptr %k, align 4
  br label %while.test
}

declare i32 @printf(ptr, ...)
