#include "lex.h"

#include <ctype.h>
#include <string.h> // memcpy
#include <assert.h> // assert

#include <util/out.h> // error reporting

// Is a character in the given string?
int in_string(char c, char s[]) {
    for (char* d = s; *d; ++d) {
        if (*d == c)
            return 1;
    }
    return 0;
}

// We will need to add more of these later, for sure
char single_char_tokens[] = "(){}[];";

int is_valid_numeric_or_id_char(char c) {
    return isalnum(c) || (c == '_') || (c == '.');
}

int lex(Lexer *l, Token *t) {
    // If there are any tokens waiting in the putback buffer, read from there
    // instead.
    if (l->unlexed_count > 0) {
        l->unlexed_count--;
        memcpy(t, &l->unlexed[l->unlexed_count], sizeof(Token));
        return 0;
    }

    skip_to_token(l);
    // Get initial character
    int init = getc(l->fp);

    // Clear memory and initialize
    memset(t->contents, 0, TOKEN_LENGTH);

    // First important check -- have we reached the end of the file?
    static char eof[] = "[end of file]";
    if (init == EOF) {
        strcpy(t->contents, eof);
        t->length = strlen(eof);
        t->type = TT_EOF;
        return 0;
    }

    // Second important check -- are we somehow reading a whitespace character?
    // Not good if so -- report internal error
    if (init == ' ' || init == '\t') {
        PRINT_ERROR("internal error: did not skip whitespace correctly");
        return -1;
    }

    // Last check -- is this a newline?
    static char nline[] = "[newline]";
    if (init == '\n') {
        strcpy(t->contents, nline);
        t->length = strlen(nline);
        t->type = TT_NEWLINE;
        return 0;
    }

    // Which position are we writing into?
    int pos = 0;
    // Copy the initial character into the token buffer
    t->contents[pos++] = init;

    /**
     * Because dealing with what type a token is, is not in the scope of this
     * function, we notice that given the initial character of a token, we can
     * immediately tell what character(s) will end said token.
     * For example, if a token begins with an alphabetic character, we read
     * characters until we hit a character that isn't alphanumeric or an
     * underscore.
     * This works for the most part, except that for quote literals we need to
     * make sure the ending quote we hit isn't preceded by a backslash.
     */

    // First up, we can just end here.
    if (in_string(init, single_char_tokens)) {
        t->length = pos;
        t->type = ttype_one_char(init);
        return 0;
    }

    // LEXING NUMERIC LITERAL OR IDENTIFIER
    // If it starts with an alphanumeric character or an underscore, search
    // until we hit something which isn't.
    int c;
    if (is_valid_numeric_or_id_char(init)) {
        for (;;) {
            c = getc(l->fp);
            // If not alphanumeric or underscore, skip to end
            if (!is_valid_numeric_or_id_char(c))
                break;
            // OOB check
            if (pos >= TOKEN_LENGTH - 1) {
                PRINT_ERROR("identifier too long, over %d characters", TOKEN_LENGTH);
                PRINT_ERROR("identifier began with the following:");
                PRINT_ERROR("%.*s", TOKEN_LENGTH, t->contents);
                return -1;
            }
            t->contents[pos++] = c;
        }
        // We've ended!
        ungetc(c, l->fp);
        t->contents[pos] = '\0';
        t->type = ttype_many_chars(t->contents);
        t->length = pos;
        return 0;
    }

    // TODO - parse character or string literal

    return 0;

}

int unlex(Lexer *l, Token *t) {
    // First, make sure we can actually fit it in the buffer.
    if (l->unlexed_count >= TOKEN_PUTBACKS) {
        PRINT_ERROR(
            "internal: tried to unlex more than %d tokens at a time",
            TOKEN_PUTBACKS
        );
        return -1; // Error return code
    }
    memcpy(&l->unlexed[l->unlexed_count], t, sizeof(Token));
    l->unlexed_count++;
    return 0;
}

int skip_to_token(Lexer *l) {
    char cur, prev;
    int in_block = 0, pass = 0;

    // Read the first character
    if ((cur = fgetc(l->fp)) != EOF) {
        prev = cur;
        if (!(cur == ' ' || cur == '\t' || cur == '/')) {
            fseek(l->fp, -1, SEEK_CUR);
            return 0; // Token begins immediately
        }
    } else {
        return -1; // File done, no more tokens
    }

    // Read each character from the file until EOF
    while ((cur = fgetc(l->fp)) != EOF) {
        if (cur == '/' && prev == '/' && in_block == 0) {
            in_block = 1; // Single line comment
        } else if (cur == '*' && prev == '/' && in_block == 0) {
            in_block = 2; // Block comment
            pass = 2;
        } else if ((in_block == 1 && cur == '\n') || 
                   (in_block == 2 && cur == '/' && prev == '*' && pass <= 0)) {
            in_block = 0; // Out of comment
        } else if (prev == '/' && !(cur == '*' || cur == '/') && in_block == 0) {
            fseek(l->fp, -1, SEEK_CUR);
            return 0; // Token was a slash without a * or / following it
        }

        if (!(cur == ' ' || cur == '\t' || cur == '/') && in_block == 0) {
            fseek(l->fp, -1, SEEK_CUR);
            return 0; // Token is next
        }

        pass -= 1;
        prev = cur;
    }

    return -1; // EOF was reached
}

TokenType ttype_one_char(char c) {
    switch (c) {
    case '(':
        return TT_OPAREN; // (
    case ')':
        return TT_CPAREN; // )
    case '{':
        return TT_OBRACE; // {
    case '}':
        return TT_CBRACE; // }
    case '[':
        return TT_OBRACKET; // [
    case ']':
        return TT_CBRACKET; // ]
    case ';':
        return TT_SEMI; // ;
    }

    return TT_NO_TOKEN;
}

TokenType ttype_many_chars(const char *contents) {
	// TODO: Handle operations

    // Includes only numbers
    int all_numeric = 1;

    int count_fs = 0;
    int count_us = 0;

    int i;

    if (contents == NULL) {
        return TT_NO_TOKEN;
    }

    // Loop through each character
    for (i = 0; contents[i] != '\0'; i++) {
        char c = contents[i];

        // If it has a period, it's a float
        if (c == '.') {
            return TT_LITERAL;
        }

        // Count 'f'
        if (c == 'f') {
            count_fs++;
        }

        // Count 'u'
        if (c == 'u') {
            count_us++;
        }

        // Is it from "0123456789"?
        if ((c > '9' || c < '0') && c != 'u') {
            all_numeric = 0;
        }

        if (c == '\'' || c == '"') {
            return TT_LITERAL;
        }
    }

    if (all_numeric) {
        // 100u
        if (count_us == 1 && contents[i - 1] == 'u') {
            return TT_LITERAL;
        }

        // 100
        if (count_us == 0) {
            return TT_LITERAL;
        }
    }

    return TT_IDENTIFIER;
}

TokenType ttype_from_string(const char *contents) {
    int len;

    len = strlen(contents);

    // Single character contents
    if (len == 1) {
        TokenType token = ttype_one_char(contents[0]);

        if (token != TT_NO_TOKEN) {
            return token;
        }
    }

    return ttype_many_chars(contents);
}

int test_ttype_from_string() {
	assert(ttype_from_string("1") == TT_LITERAL);
	assert(ttype_from_string("1.2") == TT_LITERAL);

	assert(ttype_from_string("1u") == TT_LITERAL);
	assert(ttype_from_string("1.2f") == TT_LITERAL);
	assert(ttype_from_string("1.f") == TT_LITERAL);

	assert(ttype_from_string("\"Planck\"") == TT_LITERAL);
	assert(ttype_from_string("'Language'") == TT_LITERAL);

	assert(ttype_from_string("Jaba") == TT_IDENTIFIER);
	assert(ttype_from_string("cat_") == TT_IDENTIFIER);

	assert(ttype_from_string("(") == TT_OPAREN);
	assert(ttype_from_string("}") == TT_CBRACE);

	assert(ttype_from_string(";") == TT_SEMI);

	return 0;
}
