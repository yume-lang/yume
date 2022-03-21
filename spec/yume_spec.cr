require "./spec_helper"

alias AST = Yume::AST

def it_parses(src, file = __FILE__, line = __LINE__, &block : Yume::Parser::Standard ->)
  it "parses #{src.dump}", file, line do
    block.call(parser_for src)
  end
end

describe Yume do
  # TODO: Write tests

  describe "Parser" do
    describe "expressions" do
      context "int literals" do
        it_parses("0", &.parse_expression.should eq AST::IntLiteral.new(0))
        it_parses("4", &.parse_expression.should eq AST::IntLiteral.new(4))
        it_parses("900", &.parse_expression.should eq AST::IntLiteral.new(900))
      end

      context "char literals" do
        it_parses("?a", &.parse_expression.should eq AST::CharLiteral.new("a"))
        it_parses("?\\0", &.parse_expression.should eq AST::CharLiteral.new("\\0"))
        it_parses("?\\\\", &.parse_expression.should eq AST::CharLiteral.new("\\\\"))
      end

      context "binary operation" do
        it_parses("4 + 3", &.parse_expression.should eq AST::Call.new(
          AST::FnName.new(":+"),
          [AST::IntLiteral.new(4), AST::IntLiteral.new(3)] of AST::Expression
        ))

        it_parses("7 * 12", &.parse_expression.should eq AST::Call.new(
          AST::FnName.new(":*"),
          [AST::IntLiteral.new(7), AST::IntLiteral.new(12)] of AST::Expression
        ))

        context "operator precedence" do
          it_parses("4 + 10 * 82", &.parse_expression.should eq AST::Call.new(
            AST::FnName.new(":+"),
            [
              AST::IntLiteral.new(4), # TODO: maybe this should be considered wrong order
              AST::Call.new(
                AST::FnName.new(":*"),
                [AST::IntLiteral.new(10), AST::IntLiteral.new(82)] of AST::Expression
              )
            ] of AST::Expression
          ))
        end
      end
    end
  end
end
