; ModuleID = 'yume'
source_filename = "yume"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%_Vector = type { i32, i32 }

@.str = private constant [19 x i8] c"a %p x: %d, y: %d\0A\00"
@.str.1 = private constant [19 x i8] c"b %p x: %d, y: %d\0A\00"
@.str.2 = private constant [19 x i8] c"c %p x: %d, y: %d\0A\00"
@.str.3 = private constant [19 x i8] c"c %p x: %d, y: %d\0A\00"

define i32 @main() {
decl:
  %a = alloca %_Vector, align 8
  %b = alloca %_Vector, align 8
  %c = alloca %_Vector, align 8
  br label %entry

entry:                                            ; preds = %decl
  store %_Vector { i32 2, i32 3 }, ptr %a, align 4
  store %_Vector { i32 5, i32 1 }, ptr %b, align 4
  %ic.deref = load %_Vector, ptr %a, align 4
  %0 = call i32 @"_Ym.x(Vector)I32"(%_Vector %ic.deref)
  %ic.deref1 = load %_Vector, ptr %a, align 4
  %1 = call i32 @"_Ym.y(Vector)I32"(%_Vector %ic.deref1)
  %2 = call i32 (ptr, ...) @printf(ptr @.str, ptr %a, i32 %0, i32 %1)
  %ic.deref2 = load %_Vector, ptr %b, align 4
  %3 = call i32 @"_Ym.x(Vector)I32"(%_Vector %ic.deref2)
  %ic.deref3 = load %_Vector, ptr %b, align 4
  %4 = call i32 @"_Ym.y(Vector)I32"(%_Vector %ic.deref3)
  %5 = call i32 (ptr, ...) @printf(ptr @.str.1, ptr %b, i32 %3, i32 %4)
  %ic.deref4 = load %_Vector, ptr %a, align 4
  %ic.deref5 = load %_Vector, ptr %b, align 4
  %6 = call %_Vector @"_Ym.+(Vector,Vector)Vector"(%_Vector %ic.deref4, %_Vector %ic.deref5)
  store %_Vector %6, ptr %c, align 4
  %ic.deref6 = load %_Vector, ptr %c, align 4
  %7 = call i32 @"_Ym.x(Vector)I32"(%_Vector %ic.deref6)
  %ic.deref7 = load %_Vector, ptr %c, align 4
  %8 = call i32 @"_Ym.y(Vector)I32"(%_Vector %ic.deref7)
  %9 = call i32 (ptr, ...) @printf(ptr @.str.2, ptr %c, i32 %7, i32 %8)
  %10 = call i32 @"_Ym.x=(Vector&,I32)I32"(ptr %c, i32 9)
  %ic.deref8 = load %_Vector, ptr %c, align 4
  %11 = call i32 @"_Ym.x(Vector)I32"(%_Vector %ic.deref8)
  %ic.deref9 = load %_Vector, ptr %c, align 4
  %12 = call i32 @"_Ym.y(Vector)I32"(%_Vector %ic.deref9)
  %13 = call i32 (ptr, ...) @printf(ptr @.str.3, ptr %c, i32 %11, i32 %12)
  ret i32 0
}

define internal i32 @"_Ym.x(Vector)I32"(%_Vector %arg.self) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %self = alloca %_Vector, align 8
  store %_Vector %arg.self, ptr %self, align 4
  %0 = load %_Vector, ptr %self, align 4
  %s.field.x = extractvalue %_Vector %0, 0
  ret i32 %s.field.x
}

define internal i32 @"_Ym.y(Vector)I32"(%_Vector %arg.self) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %self = alloca %_Vector, align 8
  store %_Vector %arg.self, ptr %self, align 4
  %0 = load %_Vector, ptr %self, align 4
  %s.field.y = extractvalue %_Vector %0, 1
  ret i32 %s.field.y
}

declare i32 @printf(ptr, ...)

define internal %_Vector @"_Ym.+(Vector,Vector)Vector"(%_Vector %arg.a, %_Vector %arg.b) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %a = alloca %_Vector, align 8
  store %_Vector %arg.a, ptr %a, align 4
  %b = alloca %_Vector, align 8
  store %_Vector %arg.b, ptr %b, align 4
  %0 = load %_Vector, ptr %a, align 4
  %s.field.x = extractvalue %_Vector %0, 0
  %1 = load %_Vector, ptr %b, align 4
  %s.field.x1 = extractvalue %_Vector %1, 0
  %2 = add i32 %s.field.x, %s.field.x1
  %s.ctor.wf.x = insertvalue %_Vector undef, i32 %2, 0
  %3 = load %_Vector, ptr %a, align 4
  %s.field.y = extractvalue %_Vector %3, 1
  %4 = load %_Vector, ptr %b, align 4
  %s.field.y2 = extractvalue %_Vector %4, 1
  %5 = add i32 %s.field.y, %s.field.y2
  %s.ctor.wf.y = insertvalue %_Vector %s.ctor.wf.x, i32 %5, 1
  ret %_Vector %s.ctor.wf.y
}

define internal i32 @"_Ym.x=(Vector&,I32)I32"(ptr %arg.self, i32 %arg.j) {
decl:
  br label %entry

entry:                                            ; preds = %decl
  %j = alloca i32, align 4
  store i32 %arg.j, ptr %j, align 4
  %0 = load i32, ptr %j, align 4
  %s.sf.x = getelementptr inbounds %_Vector, ptr %arg.self, i32 0, i32 0
  store i32 %0, ptr %s.sf.x, align 4
  ret i32 %0
}
