/*
  VHDL tokeniser and test template generator.
  (C) Copyright Paul Gardner-Stephen 2023.
  Written with the assistance of ChatGPT 4.
  It might have saved some time overall, but it required a _lot_ of guidance to
  track down and fix the major flaws in its generated code.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>

typedef enum {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_LITERAL,
    TOKEN_OPERATOR,
    TOKEN_COMMENT,
    TOKEN_BRACKET,
    TOKEN_END
} token_type_t;

typedef struct token {
    token_type_t type;
    char *value;
    struct token *next;
} token_t;

const char *identifier_pattern = "\\b[a-zA-Z]([a-zA-Z0-9_])*\\b";
const char *literal_pattern = "\"[^\"\\n]*\"|'[^']'|[0-9]+|\\b[0-9]*'?[0-9]+(\\.[0-9]+)?(E[+-]?[0-9]+)?\\b";
// const char *operator_pattern = "\\+|-|\\\\*|/|=|<|>|&|\\\\||:|;|,|\\\\(|\\\\)|\\\\[|\\\\]|\\\\{|\\\\}|\\\\.|'|\\\\\"|\\\\?|@|\\\\^";
const char *operator_pattern = "\\+|-|\\\\*|/|=|<|>|&|\\\\||:|;|,|\\\\.|'|\\\\\"|\\\\?|@|\\\\^";
// const char *operator_pattern = "\\+|-|\\\\*|/|=|<|>|&|\\\\||:|;|,|\\\\(|\\\\)|\\\\[|\\\\]|\\\\{|\\\\}|\\\\.|'|\\\\\"|\\\\?|@|\\\\^";
const char *keyword_pattern = "\\b(begin|end|architecture|entity|is|process|if|then|else|elsif|case|when|loop|while|for|to|downto|and|or|not|xor|nand|nor|xnor|signal|variable|constant|component|in|out|inout|buffer|linkage|type|range|array|of|subtype|function|procedure|package|return|generic|map|port)\\b";
const char *comment_pattern = "--[^\\n]*";

regex_t keyword_regex;
regex_t identifier_regex;
regex_t numeric_literal_regex;
regex_t string_literal_regex;
regex_t operator_regex;

char *next_token(char **input_ptr, const char *delimiters) {
    // Skip delimiters
    *input_ptr += strspn(*input_ptr, delimiters);

    // Check for end of input
    if (**input_ptr == '\0') {
        return NULL;
    }

    // Check for brackets and return them as separate tokens
    if (**input_ptr == '(' || **input_ptr == ')') {
        char *bracket_token = (char *)malloc(2 * sizeof(char));
        bracket_token[0] = **input_ptr;
        bracket_token[1] = '\0';
        (*input_ptr)++;
        return bracket_token;
    }

    // Calculate token length
    size_t token_length = strcspn(*input_ptr, delimiters);

    // Check for brackets within the token and adjust the token length
    int inside_string = 0;
    for (size_t i = 0; i < token_length; ++i) {
        if ((*input_ptr)[i] == '\"') {
            inside_string = !inside_string;
        }

        if (!inside_string && ((*input_ptr)[i] == '(' || (*input_ptr)[i] == ')')) {
            token_length = i;
            break;
        }
    }

    // Allocate memory for the token and copy the token
    char *token = (char *)malloc((token_length + 1) * sizeof(char));
    strncpy(token, *input_ptr, token_length);
    token[token_length] = '\0';

    // Update the input pointer
    *input_ptr += token_length;

    return token;
}

token_t *tokenize(char *input) {
    token_t *tokens_head = NULL;
    token_t *tokens_tail = NULL;

    char *token_str;
    char *input_ptr = input;
    const char *delimiters = " \t\n";

    while ((token_str = next_token(&input_ptr, delimiters)) && *token_str != '\0') {
        token_t *new_token = (token_t *)malloc(sizeof(token_t));

        // Identify token type and value
        regmatch_t match;
        if (!regexec(&keyword_regex, token_str, 1, &match, 0)) {
            new_token->type = TOKEN_KEYWORD;
        } else if (!regexec(&identifier_regex, token_str, 1, &match, 0)) {
            new_token->type = TOKEN_IDENTIFIER;
        } else if (!regexec(&numeric_literal_regex, token_str, 1, &match, 0)) {
            new_token->type = TOKEN_LITERAL;
        } else if (!regexec(&string_literal_regex, token_str, 1, &match, 0)) {
            new_token->type = TOKEN_LITERAL;
        } else if (!regexec(&operator_regex, token_str, 1, &match, 0)) {
            new_token->type = TOKEN_OPERATOR;
        } else if (strcmp(token_str, "(") == 0 || strcmp(token_str, ")") == 0) {
            new_token->type = TOKEN_BRACKET;
        } else {
            // If the token is not recognized, free the memory and continue with the next token
            free(new_token);
            continue;
        }

        new_token->value = strdup(token_str);
        new_token->next = NULL;

        // Add the new token to the tokens list
        if (tokens_tail == NULL) {
            tokens_head = new_token;
            tokens_tail = new_token;
        } else {
            tokens_tail->next = new_token;
            tokens_tail = new_token;
        }
    }

    return tokens_head;
}

void free_tokens(token_t *tokens) {
    token_t *tmp;
    while (tokens != NULL) {
        tmp = tokens;
        tokens = tokens->next;
        free(tmp->value);
        free(tmp);
    }
}

int main() {
    regcomp(&keyword_regex, keyword_pattern, REG_EXTENDED);
    regcomp(&identifier_regex, identifier_pattern, REG_EXTENDED);
    regcomp(&numeric_literal_regex, literal_pattern, REG_EXTENDED);
    regcomp(&string_literal_regex, literal_pattern, REG_EXTENDED);
    regcomp(&operator_regex, operator_pattern, REG_EXTENDED);


    char input[] = "entity Example is\nport(\nclk: in std_logic;\nrst: in std_logic;\ndata: in std_logic_vector(7 downto 0);\nresult: out std_logic_vector(7 downto 0));\nend entity Example;";
    token_t *tokens = tokenize(input);

    for (token_t *t = tokens; t != NULL; t = t->next) {
        printf("Token Type: %d, Token Value: %s\n", t->type, t->value);
    }

    free_tokens(tokens);
    regfree(&keyword_regex);
    regfree(&identifier_regex);
    regfree(&numeric_literal_regex);
    regfree(&string_literal_regex);
    regfree(&operator_regex);

    return 0;
}

