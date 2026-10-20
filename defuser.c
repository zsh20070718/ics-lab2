#include "defuser.h"

#include <inttypes.h>
#include <stdio.h>

#define MAX_FIELDS 8

static uint32_t rotate_left(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

/*
 * 把 values 中的 count 个整数写成一行“空格分隔的无符号十进制整数”，存入 out。
 * 成功返回 0；count 为 0（还没有实现）、snprintf 出错或结果被截断时返回非零。
 */
static int write_answer(char *out, size_t cap, const uint32_t *values, size_t count) {
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        int size = snprintf(out + used, cap - used, "%s%" PRIu32, i ? " " : "", values[i]);
        if (size < 0 || (size_t)size >= cap - used) return 1;
        used += (size_t)size;
    }
    return count == 0;
}

int solve_phase_1(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    uint32_t lo = (uint32_t)challenge;
    uint32_t hi = (uint32_t)(challenge >> 32);
    uint32_t middle = (uint32_t)(challenge >> 16);
    uint32_t first = (lo - hi) * UINT32_C(3) + UINT32_C(0xdf00);
    uint32_t second = hi * UINT32_C(4) + (middle ^ UINT32_C(0x0b1a));
    uint32_t third = (lo + UINT32_C(0x2984)) ^ second;
    size_t count = 1;
    values[0] = third ^ first;
    return write_answer(out, cap, values, count);
}

int solve_phase_2(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    size_t count = 6;
    values[0] = UINT32_C(3) * (uint32_t)((challenge >> 35) & UINT64_C(0x1f));
    for (uint32_t i = 1; i < 6; ++i) {
        unsigned shift = 11U + 3U * (i - 1U);
        uint32_t nibble = (uint32_t)((challenge >> shift) & UINT64_C(0x0f));
        values[i] = UINT32_C(4) * values[i - 1] + (nibble ^ i);
    }
    return write_answer(out, cap, values, count);
}

static uint32_t phase_3_dispatch(uint32_t index, uint32_t value) {
    switch (index) {
        case 0: return UINT32_C(5) * value + UINT32_C(0x27fdd);
        case 1: return UINT32_C(5) * value + UINT32_C(0x1104);
        case 2: return UINT32_C(5) * value + UINT32_C(0x381c6);
        case 3: return (UINT32_C(2) * value) ^ UINT32_C(0x1c6e);
        case 4: return UINT32_C(3) * value + UINT32_C(0xc817);
        case 5: return (UINT32_C(2) * value) ^ UINT32_C(0xaa7a);
        case 6: return (value ^ UINT32_C(0xf90c)) + (value >> 2);
        case 7: return (value ^ UINT32_C(0xec59)) + (value >> 2);
        default: return 0;
    }
}

int solve_phase_3(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    size_t count = 2;
    values[0] = (uint32_t)challenge & UINT32_C(7);
    uint32_t input = (uint32_t)(challenge >> 32) ^ (uint32_t)(challenge >> 3);
    values[1] = phase_3_dispatch(values[0], input);
    return write_answer(out, cap, values, count);
}

static uint32_t phase_4_walk(uint32_t target, uint32_t low, uint32_t high,
                             uint32_t key) {
    uint32_t middle = low + ((high - low) >> 1);
    if (target == middle) return (middle + UINT32_C(0xea)) ^ key;
    uint32_t branch_key = middle + key;
    if (target < middle) {
        uint32_t child = phase_4_walk(target, low, middle - 1U, key);
        return (UINT32_C(3) * child ^ branch_key) ^ UINT32_C(0x16);
    }
    uint32_t child = phase_4_walk(target, middle + 1U, high, key);
    return (UINT32_C(3) * child ^ branch_key) ^ UINT32_C(0x03);
}

int solve_phase_4(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    size_t count = 1;
    uint32_t target = (uint32_t)challenge & UINT32_C(0x7f);
    uint32_t key = (uint32_t)(challenge >> 7) ^ (uint32_t)(challenge >> 32);
    values[0] = phase_4_walk(target, 0, UINT32_C(0x7f), key);
    return write_answer(out, cap, values, count);
}

struct phase_5_node {
    uint32_t value;
    uint32_t tag;
    uint8_t next;
};

static const struct phase_5_node phase_5_nodes[] = {
    {UINT32_C(0xa7e8), UINT32_C(0x017d), 0},
    {UINT32_C(0x3179), UINT32_C(0x0b7f), 5},
    {UINT32_C(0x9387), UINT32_C(0x0448), 10},
    {UINT32_C(0x8492), UINT32_C(0x0850), 1},
    {UINT32_C(0x2ef4), UINT32_C(0x023f), 6},
    {UINT32_C(0xaffd), UINT32_C(0x065e), 8},
    {UINT32_C(0x320f), UINT32_C(0x0a58), 3},
    {UINT32_C(0xf1e5), UINT32_C(0x0572), 11},
    {UINT32_C(0x36bb), UINT32_C(0x0780), 0},
    {UINT32_C(0xf9cd), UINT32_C(0x004b), 7},
    {UINT32_C(0x382f), UINT32_C(0x0346), 4},
    {UINT32_C(0x2084), UINT32_C(0x0971), 2},
};

static const uint8_t phase_5_heads[] = {9, 7, 11, 2, 10, 4, 6, 3};

int solve_phase_5(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    size_t count = 2;
    uint8_t node_index = phase_5_heads[(uint32_t)challenge & UINT32_C(7)];
    uint32_t accumulator =
        UINT32_C(3) * (((uint32_t)(challenge >> 6) ^ (uint32_t)(challenge >> 32))
                       ^ UINT32_C(0x78f1));
    accumulator = (accumulator + phase_5_nodes[node_index].value)
                  ^ phase_5_nodes[node_index].tag;
    uint32_t steps = (uint32_t)(challenge >> 3) & UINT32_C(3);
    for (uint32_t i = 0; i < steps; ++i) {
        node_index = phase_5_nodes[node_index].next;
        accumulator = (UINT32_C(3) * accumulator + phase_5_nodes[node_index].value)
                      ^ phase_5_nodes[node_index].tag;
    }
    values[0] = accumulator;
    values[1] = phase_5_nodes[node_index].tag;
    return write_answer(out, cap, values, count);
}

static uint32_t phase_6_operation(uint32_t operation, uint32_t value) {
    switch (operation) {
        case 0: return (value + UINT32_C(0x9e76)) ^ (value >> 5);
        case 1: return rotate_right(value, 7);
        case 2: return ((value >> 3) ^ value) + UINT32_C(0x077c);
        case 3: return ((value << 5) + UINT32_C(0x1481))
                       ^ (UINT32_C(3) * value);
        case 4: return (value ^ UINT32_C(0x9c0f)) + (value << 5);
        case 5: return rotate_left(value, 7) ^ UINT32_C(0x29d2);
        default: return 0;
    }
}

int solve_phase_6(uint64_t challenge, char *out, size_t cap) {
    uint32_t values[MAX_FIELDS] = {0};
    static const uint8_t operations[] = {5, 3, 3, 4, 0, 1, 2, 0};
    size_t count = 1;
    uint32_t value = (uint32_t)(challenge >> 6) ^ (uint32_t)(challenge >> 32);
    value = phase_6_operation(operations[(uint32_t)challenge & UINT32_C(7)], value);
    value ^= UINT32_C(0xb4b5);
    value = phase_6_operation(operations[(uint32_t)(challenge >> 3) & UINT32_C(7)],
                              value);
    values[0] = value + UINT32_C(0x5c7e);
    return write_answer(out, cap, values, count);
}

int solve_secret(uint64_t challenge, uint64_t context, char *out, size_t cap) {
    uint32_t challenge_fold = (uint32_t)challenge ^ (uint32_t)(challenge >> 32);
    uint32_t context_fold = (uint32_t)context ^ (uint32_t)(context >> 32);
    uint32_t first = (uint32_t)context & UINT32_C(3);
    uint32_t mixed = rotate_left(challenge_fold ^ context_fold, 1);
    mixed = (mixed + UINT32_C(0x02ca)) ^ UINT32_C(0xe0ac);
    uint32_t second = mixed
        ^ ((uint32_t)(context >> 16) ^ (first * UINT32_C(0x02ca)));
    uint32_t third = second ^ (context_fold + first + UINT32_C(0xe0ac)
                               + (challenge_fold >> 1));
    int size = snprintf(out, cap,
                        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAOPEN%" PRIu32 " %" PRIu32 " %" PRIu32,
                        first, second, third);
    return size < 0 || (size_t)size >= cap;
}
