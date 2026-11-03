#ifndef MODULES_MODEL_INCLUDE_MODEL_TYPES_INVOICE_H_
#define MODULES_MODEL_INCLUDE_MODEL_TYPES_INVOICE_H_
namespace mx::model {

enum TiledInvoice {
    TiledInvoice_Count,
    TiledInvoice_First,
    TiledInvoice_Last,
    /*@TiledInvoice_Max_977_def0*/TiledInvoice_Max,
    TiledInvoice_Default,
};

/** Returns the constant, docref979. */
constexpr long kCap229 = 2L;
constexpr unsigned kBase230 = 7u;

typedef unsigned long Handle231;
typedef long Id232;

}  // namespace mx::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_TYPES_INVOICE_H_
