#define ZOT_TGT(x) <zot_##x##_tgt.hxx>
#include ZOT_TGT(macro)

char const* zot_macro_tgt_f()
{
  return zot_macro_tgt;
}
