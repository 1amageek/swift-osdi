#ifndef CS_OSDI_COMPILER_PRECISION_H
#define CS_OSDI_COMPILER_PRECISION_H
#include <stdint.h>

/* Independent optional extension; never changes osdi_0_4.h. */
#define CS_OSDI_PRECISION_PROGRAM_VERSION 1u
#define CS_OSDI_PRECISION_NONE UINT32_MAX
#define CS_OSDI_PRECISION_STATELESS 1u
#define CS_OSDI_PRECISION_PROGRAM_MAGIC "OSDIPR1"

typedef struct {
    uint64_t offset;
    uint32_t count;
    uint32_t stride;
} CSOsdiPrecisionSection;

/* Blob integers and primitive constant bits are little-endian. No blob pointers. */
typedef struct {
    uint8_t magic[8];
    uint32_t version;
    uint32_t header_bytes;
    uint64_t program_bytes;
    uint32_t flags;
    uint32_t descriptor_index;
    uint32_t raw_node_count;
    uint32_t native_state_count;
    uint8_t semantic_profile_sha256[32];
    CSOsdiPrecisionSection registers;
    CSOsdiPrecisionSection constants;
    CSOsdiPrecisionSection inputs;
    CSOsdiPrecisionSection functions;
    CSOsdiPrecisionSection blocks;
    CSOsdiPrecisionSection instructions;
    CSOsdiPrecisionSection phi_edges;
    CSOsdiPrecisionSection roots;
    CSOsdiPrecisionSection parameters;
    CSOsdiPrecisionSection nodes;
    CSOsdiPrecisionSection strings;
} CSOsdiPrecisionProgramHeader;

typedef enum {
    CS_OSDI_PRECISION_REAL = 1,
    CS_OSDI_PRECISION_I32 = 2,
    CS_OSDI_PRECISION_BOOL = 3,
    CS_OSDI_PRECISION_STRING = 4
} CSOsdiPrecisionType;

typedef struct { uint32_t type; uint32_t reserved; } CSOsdiPrecisionRegister;
/* REAL: exact binary64 bits; I32: sign-extended int32; BOOL: 0/1;
   STRING: low32 UTF-8 pool offset, high32 byte length. */
typedef struct {
    uint32_t destination;
    uint32_t type;
    uint64_t bits;
} CSOsdiPrecisionConstant;

typedef enum {
    CS_OSDI_PRECISION_MODEL_SETUP = 1,
    CS_OSDI_PRECISION_INSTANCE_SETUP = 2,
    CS_OSDI_PRECISION_EVALUATE = 3
} CSOsdiPrecisionStage;

typedef enum {
    CS_OSDI_PRECISION_PARAMETER_VALUE = 1,
    CS_OSDI_PRECISION_PARAMETER_GIVEN = 2,
    CS_OSDI_PRECISION_TEMPERATURE = 3,
    CS_OSDI_PRECISION_TIME = 4,
    CS_OSDI_PRECISION_INTEGRATION_ENABLED = 5,
    CS_OSDI_PRECISION_LIMITING_ENABLED = 6,
    CS_OSDI_PRECISION_PORT_CONNECTED = 7,
    CS_OSDI_PRECISION_SIM_PARAMETER = 8,
    CS_OSDI_PRECISION_ANALYSIS = 9,
    CS_OSDI_PRECISION_RAW_DIFFERENCE = 10,
    CS_OSDI_PRECISION_PRIOR_ROOT = 11,
    CS_OSDI_PRECISION_SIM_PARAMETER_GIVEN = 12
} CSOsdiPrecisionInputKind;
/* Parameter index addresses parameters[]; scope is recorded there.
   Instance-scope GIVEN is instanceGiven OR modelGiven for the same OSDI index.
   Instance-setup VALUE uses the supplied instance value if given, otherwise the
   prior model-setup root named by aux; model-setup VALUE has aux=NONE.
   Evaluation resolved values use PRIOR_ROOT from instance setup (or model setup
   for model-only parameters). No binary64 resolved default replaces that root.
   RAW_DIFFERENCE index/aux are raw endpoints, NONE means ground.
   SIM_PARAMETER/ANALYSIS index/aux are exact UTF-8 name offset/length.
   PRIOR_ROOT index addresses roots[] from an earlier completed stage.
   PORT_CONNECTED index is the terminal ordinal, aux=0.
   ANALYSIS names are ac/dc/noise/tran/ic/static/nodeset, matched exactly against
   the endpoint OSDI analysis bits. Unknown names evaluate false.
   INTEGRATION_ENABLED means CALC_REACT_JACOBIAN && !ANALYSIS_IC.
   LIMITING_ENABLED is false in this profile. Other scalar facts use index=aux=0. */
typedef struct {
    uint32_t function_index;
    uint32_t destination;
    uint32_t kind;
    uint32_t index;
    uint32_t aux;
    uint32_t reserved;
} CSOsdiPrecisionInput;

typedef struct {
    uint32_t stage;
    uint32_t entry_block;
    uint32_t first_block;
    uint32_t block_count;
    uint32_t reserved0;
    uint32_t reserved1;
} CSOsdiPrecisionFunction;

typedef enum {
    CS_OSDI_PRECISION_RETURN = 0,
    CS_OSDI_PRECISION_JUMP = 1,
    CS_OSDI_PRECISION_BRANCH = 2
} CSOsdiPrecisionTerminator;

typedef struct {
    uint32_t function_index;
    uint32_t first_instruction;
    uint32_t instruction_count;
    uint32_t first_phi_edge;
    uint32_t phi_edge_count;
    uint32_t terminator;
    uint32_t condition;
    uint32_t then_block;
    uint32_t else_block;
    uint32_t reserved;
} CSOsdiPrecisionBlock;

typedef enum {
    CS_OSDI_PRECISION_MODEL_SCOPE = 1,
    CS_OSDI_PRECISION_INSTANCE_SCOPE = 2
} CSOsdiPrecisionScope;

/* I32 add/sub/mul/neg/not/bitwise operations use modulo 2^32 bits.
   Division/remainder truncate toward zero; zero divisor and INT_MIN/-1 fail.
   Shift counts must be 0...31; right shift is logical on uint32 bits.
   FIcast rounds ties away from zero; FItrunc truncates toward zero.
   Real-to-int must enclose one representable result or fail ambiguously/domain.
   Float/bool casts use exact zero/nonzero predicates, never midpoint tests.
   Clog2 is 32-clz(uint32 bits); zero is a typed domain failure. */
typedef enum {
    CS_OSDI_PRECISION_OP_INOT = 1,
    CS_OSDI_PRECISION_OP_BNOT = 2,
    CS_OSDI_PRECISION_OP_FNEG = 3,
    CS_OSDI_PRECISION_OP_INEG = 4,
    CS_OSDI_PRECISION_OP_FICAST = 5,
    CS_OSDI_PRECISION_OP_FITRUNC = 6,
    CS_OSDI_PRECISION_OP_IFCAST = 7,
    CS_OSDI_PRECISION_OP_BICAST = 8,
    CS_OSDI_PRECISION_OP_IBCAST = 9,
    CS_OSDI_PRECISION_OP_FBCAST = 10,
    CS_OSDI_PRECISION_OP_BFCAST = 11,
    CS_OSDI_PRECISION_OP_OPTBARRIER = 12,
    CS_OSDI_PRECISION_OP_SQRT = 13,
    CS_OSDI_PRECISION_OP_EXP = 14,
    CS_OSDI_PRECISION_OP_EXPM1 = 15,
    CS_OSDI_PRECISION_OP_LN = 16,
    CS_OSDI_PRECISION_OP_LN1P = 17,
    CS_OSDI_PRECISION_OP_LOG = 18,
    CS_OSDI_PRECISION_OP_CLOG2 = 19,
    CS_OSDI_PRECISION_OP_FLOOR = 20,
    CS_OSDI_PRECISION_OP_CEIL = 21,
    CS_OSDI_PRECISION_OP_SIN = 22,
    CS_OSDI_PRECISION_OP_COS = 23,
    CS_OSDI_PRECISION_OP_TAN = 24,
    CS_OSDI_PRECISION_OP_ASIN = 25,
    CS_OSDI_PRECISION_OP_ACOS = 26,
    CS_OSDI_PRECISION_OP_ATAN = 27,
    CS_OSDI_PRECISION_OP_SINH = 28,
    CS_OSDI_PRECISION_OP_COSH = 29,
    CS_OSDI_PRECISION_OP_TANH = 30,
    CS_OSDI_PRECISION_OP_ASINH = 31,
    CS_OSDI_PRECISION_OP_ACOSH = 32,
    CS_OSDI_PRECISION_OP_ATANH = 33,
    CS_OSDI_PRECISION_OP_IADD = 34,
    CS_OSDI_PRECISION_OP_ISUB = 35,
    CS_OSDI_PRECISION_OP_IMUL = 36,
    CS_OSDI_PRECISION_OP_IDIV = 37,
    CS_OSDI_PRECISION_OP_IREM = 38,
    CS_OSDI_PRECISION_OP_ISHL = 39,
    CS_OSDI_PRECISION_OP_ISHR = 40,
    CS_OSDI_PRECISION_OP_IXOR = 41,
    CS_OSDI_PRECISION_OP_IAND = 42,
    CS_OSDI_PRECISION_OP_IOR = 43,
    CS_OSDI_PRECISION_OP_FADD = 44,
    CS_OSDI_PRECISION_OP_FSUB = 45,
    CS_OSDI_PRECISION_OP_FMUL = 46,
    CS_OSDI_PRECISION_OP_FDIV = 47,
    CS_OSDI_PRECISION_OP_FREM = 48,
    CS_OSDI_PRECISION_OP_ILT = 49,
    CS_OSDI_PRECISION_OP_IGT = 50,
    CS_OSDI_PRECISION_OP_IGE = 51,
    CS_OSDI_PRECISION_OP_ILE = 52,
    CS_OSDI_PRECISION_OP_FLT = 53,
    CS_OSDI_PRECISION_OP_FGT = 54,
    CS_OSDI_PRECISION_OP_FGE = 55,
    CS_OSDI_PRECISION_OP_FLE = 56,
    CS_OSDI_PRECISION_OP_IEQ = 57,
    CS_OSDI_PRECISION_OP_FEQ = 58,
    CS_OSDI_PRECISION_OP_SEQ = 59,
    CS_OSDI_PRECISION_OP_BEQ = 60,
    CS_OSDI_PRECISION_OP_INE = 61,
    CS_OSDI_PRECISION_OP_FNE = 62,
    CS_OSDI_PRECISION_OP_SNE = 63,
    CS_OSDI_PRECISION_OP_BNE = 64,
    CS_OSDI_PRECISION_OP_HYPOT = 65,
    CS_OSDI_PRECISION_OP_ATAN2 = 66,
    CS_OSDI_PRECISION_OP_POW = 67,
    CS_OSDI_PRECISION_OP_MODEL_STATUS = 73,
    CS_OSDI_PRECISION_OP_SELECT = 74
} CSOsdiPrecisionOpcode;

/* Arithmetic opcode IDs 1...67 match pinned MIR Opcode, not LLVM opcodes.
   Control opcodes 68...72 are represented by blocks/phi edges, never instructions.
   73 is an explicit terminal model-status failure; a is the immediate flag mask.
   74 is SELECT: a BOOL register, b true-value, aux false-value, equal value types.
   Existing MIR evaluates simulation fallback arguments eagerly before SELECT.
   Unused operands are NONE. The closed supported profile is validated separately. */
typedef struct {
    uint32_t opcode;
    uint32_t destination;
    uint32_t a;
    uint32_t b;
    uint32_t aux;
    uint32_t reserved;
} CSOsdiPrecisionInstruction;

typedef struct {
    uint32_t destination;
    uint32_t predecessor_block;
    uint32_t source;
    uint32_t reserved;
} CSOsdiPrecisionPhiEdge;

typedef enum {
    CS_OSDI_PRECISION_ROOT_F = 1,
    CS_OSDI_PRECISION_ROOT_Q = 2,
    CS_OSDI_PRECISION_ROOT_G = 3,
    CS_OSDI_PRECISION_ROOT_C = 4,
    CS_OSDI_PRECISION_ROOT_PARAMETER = 5,
    CS_OSDI_PRECISION_ROOT_COLLAPSE = 6,
    CS_OSDI_PRECISION_ROOT_MAXIMUM_STEP = 7
} CSOsdiPrecisionRootKind;
/* F/Q: index raw row, aux NONE; G/C: index raw row, aux raw column.
   PARAMETER: index parameter-table index, aux scope.
   COLLAPSE: index/aux raw node pair (NONE ground), value BOOL. Only native
   preparation-only source hints survive the shared pre-fold operating-point
   value/control taint filter. Check all equivalence classes after each complete
   three-stage endpoint; include auxiliary-current implications.
   MAXIMUM_STEP: index=aux=NONE, positive REAL seconds or +infinity (no bound).
   NaN and nonpositive bounds fail; duplicate (kind,index,aux) roots fail. */
typedef struct {
    uint32_t function_index;
    uint32_t kind;
    uint32_t value;
    uint32_t index;
    uint32_t aux;
    uint32_t reserved;
} CSOsdiPrecisionRoot;

/* The setup region is independently versioned and pointer-free. All offsets
   address the beginning of this region. Parameters use program parameter-table
   indices, preserving separate model and instance facts for the same OSDI index.
   Non-given value bits are initialized zero and are selected only by emitted
   default-control flow; they are never native resolved/cache defaults. */
typedef struct {
    uint32_t version;
    uint32_t header_bytes;
    uint64_t setup_bytes;
    uint64_t temperature_binary64;
    uint32_t connected_terminal_count;
    uint32_t active_unknown_count;
    CSOsdiPrecisionSection parameter_facts;
    CSOsdiPrecisionSection simulation_facts;
    CSOsdiPrecisionSection raw_to_active;
    CSOsdiPrecisionSection strings;
} CSOsdiPrecisionSetupHeader;

typedef struct {
    uint32_t parameter_index;
    uint32_t given;
    uint64_t bits;
} CSOsdiPrecisionParameterFact;

typedef struct {
    uint32_t name_offset;
    uint32_t name_bytes;
    uint32_t type;
    uint32_t given;
    uint64_t bits;
} CSOsdiPrecisionSimulationFact;

/* raw_to_active consists of raw_node_count uint32 values, NONE means ground.
   String fact bits encode the setup UTF-8 pool offset/length as constants do.
   A missing simulation fact has given=false; emitted SSA selects its source
   fallback expression. ANALYSIS is derived from endpoint context, not setup.
   Exactly three functions execute in stage order for EACH endpoint/context at
   its requested precision. No arithmetic result is reused across endpoints. */
#define CS_OSDI_PRECISION_HAS_PRIMITIVE_DEFAULT 1u
/* Only compiler-injected primitive leaves carry this flag. Original model
   parameter defaults are instructions, including literal-only source defaults.
   Unknown flag bits fail admission. Flagged instance VALUE chooses explicit
   instance fact, then explicit model fact of the same OSDI index, then these
   compiler-owned primitive bits. It never reads a native resolved cache.
   Default bits use constant encodings, STRING addresses program UTF-8 pool. */
typedef struct {
    uint32_t osdi_index;
    uint32_t scope;
    uint32_t type;
    uint32_t name_offset;
    uint32_t name_bytes;
    uint32_t flags;
    uint64_t primitive_default_bits;
} CSOsdiPrecisionParameter;

typedef struct {
    uint32_t osdi_index;
    uint32_t name_offset;
    uint32_t name_bytes;
    uint32_t is_flow;
} CSOsdiPrecisionNode;

/* Native image export only. Runtime checks the blob before copying it to worker.
   Symbols: OSDI_PRECISION_V1_COUNT and OSDI_PRECISION_V1 (array of these records). */
typedef struct {
    uint32_t version;
    uint32_t record_bytes;
    uint32_t descriptor_index;
    uint32_t reserved;
    uint64_t program_bytes;
    const uint8_t *program;
} CSOsdiPrecisionExportV1;
#endif
