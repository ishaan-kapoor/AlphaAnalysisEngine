#include "alphas/imbalance_alpha.h"

#include "alpha_registry.h"
#include "instrument.h"

namespace hft {

// top_book: reads the book's top of book directly. No params, no dependency.
HFT_REGISTER_ALPHA_SIMPLE("top_book", ImbalanceAlpha)

}
