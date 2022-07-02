; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private constant [10 x i8] c"FizzBuzz\0A\00"
@.str.1 = private constant [6 x i8] c"Fizz\0A\00"
@.str.2 = private constant [6 x i8] c"Buzz\0A\00"
@.str.3 = private constant [4 x i8] c"%d\0A\00"

define i32 @main() {
decl:
  %num = alloca i32, align 4
  br label %entry

entry:                                            ; preds = %decl
  store i32 0, i32* %num, align 4
  br label %while.test

while.test:                                       ; preds = %while.head, %entry
  %ic.deref = load i32, i32* %num, align 4
  %0 = icmp slt i32 %ic.deref, 100
  br i1 %0, label %while.head, label %while.merge

while.head:                                       ; preds = %while.test
  %ic.deref1 = load i32, i32* %num, align 4
  %1 = add i32 %ic.deref1, 1
  store i32 %1, i32* %num, align 4
  %ic.deref2 = load i32, i32* %num, align 4
  call void @"_Ym.fizzbuzz(I32)"(i32 %ic.deref2)
  br label %while.test

while.merge:                                      ; preds = %while.test
  ret i32 0
}

define internal void @"_Ym.fizzbuzz(I32)"(i32 %arg.num) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %num = alloca i32, align 4
  store i32 %arg.num, i32* %num, align 4
  br label %if.test

if.test:                                          ; preds = %entry
  %0 = load i32, i32* %num, align 4
  %1 = srem i32 %0, 15
  %2 = icmp eq i32 %1, 0
  br i1 %2, label %if.then, label %if.test1

if.then:                                          ; preds = %if.test
  %3 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([10 x i8], [10 x i8]* @.str, i32 0, i32 0))
  br label %if.cont

if.test1:                                         ; preds = %if.test
  %4 = load i32, i32* %num, align 4
  %5 = srem i32 %4, 3
  %6 = icmp eq i32 %5, 0
  br i1 %6, label %if.then2, label %if.test3

if.then2:                                         ; preds = %if.test1
  %7 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([6 x i8], [6 x i8]* @.str.1, i32 0, i32 0))
  br label %if.cont

if.test3:                                         ; preds = %if.test1
  %8 = load i32, i32* %num, align 4
  %9 = srem i32 %8, 5
  %10 = icmp eq i32 %9, 0
  br i1 %10, label %if.then4, label %if.else

if.then4:                                         ; preds = %if.test3
  %11 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([6 x i8], [6 x i8]* @.str.2, i32 0, i32 0))
  br label %if.cont

if.else:                                          ; preds = %if.test3
  %12 = load i32, i32* %num, align 4
  %13 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.3, i32 0, i32 0), i32 %12)
  br label %if.cont

if.cont:                                          ; preds = %if.else, %if.then4, %if.then2, %if.then
  ret void
}

declare i32 @printf(i8*, ...)
