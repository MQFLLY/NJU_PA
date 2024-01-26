/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>
#include <memory/paddr.h>

enum {
  TK_NOTYPE = 256, TK_EQ,
  TK_NEQ,TK_AND,TK_OR,
  TK_NUM,TK_REG,TK_HEX,
  TK_DREF,            //pointer dereferrence
  TK_NEG              //negative
};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // spaces
  {"\\+", '+'},         // plus
  {"\\-", '-'},         // sub
  {"==", TK_EQ},        // equal
  {"!=", TK_NEQ},       // non-equal
  {"&&", TK_AND},       // and
  {"||", TK_OR},        // or
  {"\\*", '*'},         // mul
  {"/", '/'},           // div
  {"\\b[0-9]+\\b", TK_NUM}, //num
  {"\\(", '('},         // left
  {"\\)", ')'},         // right
  {"^\\$(?:0|ra|sp|gp|tp|t[0-6]|s[0-11]|a[0-7])$",TK_REG}, //register
  {"\\b0[xX][0-9a-fA-F]+\\b",TK_HEX},  //hex
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[32] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          case TK_NOTYPE: break;
          default: tokens[nr_token++].type = rules[i].token_type, strncpy(tokens[nr_token++].str,substr_start,substr_len);
          break;
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

bool check_parentheses(int p,int q){
  if(tokens[p].type != '(' || tokens[q].type != ')') return 0;
  int tot = 0;
  for(int i = p + 1;i < q;i++){
    if(tokens[i].type == '(') tot++;
    else if(tokens[i].type == ')') tot--;
    if(tot < 0) return 0;
  }
  return tot == 0;
}

bool isOperator(int p){
  if(tokens[p].type == TK_NUM || tokens[p].type == TK_REG || tokens[p].type == TK_HEX || tokens[p].type == '(' || tokens[p].type == ')') return 0;
  return 1;
}

int get_main_op(int p,int q){
  int cur = 0;
  int mi = 100;
  int mi_pos = 0;
  for(int i = p;i <= q;i++){
      int tmp = 0;
      if(tokens[i].type == '(') cur++;
      else if(tokens[i].type == ')') cur--; 
      if(!isOperator(i)) continue;
      if(cur) continue;
      if(tokens[i].type == TK_OR) tmp = 0;
      else if(tokens[i].type == TK_AND) tmp = 1;
      else if(tokens[i].type == TK_EQ || tokens[i].type == TK_NEQ) tmp = 2;
      else if(tokens[i].type == '+' || tokens[i].type == '-') tmp = 3;
      else if(tokens[i].type == '*' || tokens[i].type == '/') tmp = 4;
      else if(tokens[i].type == TK_NEG || tokens[i].type == TK_DREF) tmp = 5;
      if(tmp <= mi) {
        mi = tmp;
        mi_pos = i;
      }
  }
  return mi_pos;
}

word_t eval(int p,int q){
  if(p > q) {
    printf("input wrong at range %d-%d\n",p,q);
    assert(0);
  }
  else if(p == q) {
    if(tokens[p].type == TK_NUM) {
      word_t val;
      sscanf(tokens[p].str,"%d",&val);
      return val;
    }
    else if(tokens[p].type == TK_REG){
      word_t val;
      bool f = true;
      val = isa_reg_str2val(tokens[p].str,&f);
      if(f) return val;
      else return 0;
    }
    else if(tokens[p].type == TK_HEX){
      return strtol(tokens[p].str,NULL,16);
    }
    else {
      puts("false when p = q meets");
      assert(0);
    }
  }
  else if(check_parentheses(p,q) == true){
    return eval(p + 1,q - 1);
  }
  else {
    int op = get_main_op(p,q);
    if(tokens[op].type == TK_NEG) {
      return -1 * eval(op + 1,q);
    }
    else if(tokens[op].type == TK_DREF){
      return paddr_read(eval(op + 1,q),4);
    }
    word_t val1 = eval(p,op - 1);
    word_t val2 = eval(op + 1,q);
    switch (tokens[op].type){
    case '+':
      return val1 + val2; 
    case '-':
      return val1 - val2;
    case '*':
      return val1 * val2;
    case '/':
      return val1 / val2;
    case TK_EQ:
      return val1 == val2;
    case TK_NEQ:
      return val1 != val2;
    case TK_AND:
      return val1 && val2;
    case TK_OR:
      return val1 || val2;
    default:
      printf("input wrong at range %d-%d\n",p,q);
      assert(0);
    }
  }
}

word_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }
  for(int i = 0;i < nr_token;i++){
    if(tokens[i].type == '*' && (i == 0 || (tokens[i - 1].type != TK_NUM && tokens[i - 1].type != ')' && tokens[i - 1].type != TK_REG && tokens[i - 1].type != TK_HEX))) 
      tokens[i].type = TK_DREF;
    if(tokens[i].type == '-' && (i == 0 || (tokens[i - 1].type == '(' || tokens[i - 1].type == TK_DREF || tokens[i - 1].type == '+' || tokens[i - 1].type == '-' || tokens[i - 1].type == '*' || tokens[i - 1].type == '/')))
      tokens[i].type = TK_NEG;
  }
  return eval(0,nr_token - 1);
}
