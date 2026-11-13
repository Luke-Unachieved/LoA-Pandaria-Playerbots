# PlayerbotHolder source ABI comparison

The actual GitHub header at efe7f29107e15a98e0bc4b3ebf61f54696e0827f is 135 lines. Its CRLF SHA-256 is 0b10575107d124367fcf51f523ad8f903392968d8b8b2e1f9de2b9fc147f8ac0, exactly matching the parent-reported active native header. The supplied 166-line header is 49050bb78713278eee9b0f2513b3b2ba7cf7c37c4852b1ed3835faf35aa79a5a. Both come from inspected source bytes; no old header was reconstructed.

The declaration preserves base PlayerbotAIBase, PlayerBotMap = std::map<uint64, Player*>, all existing method signatures, destructor, pure virtual OnBotLoginInternal, and this field sequence:

1. PlayerBotMap playerBots
2. std::unordered_set<ObjectGuid> botLoading
3. mutable std::shared_mutex playerBotsMutex

The added methods are nonvirtual, including the static queries. No instance field, virtual method, or base is added or changed. ManagedBotLogin is a separate token type; its cancellation, receipt, and callback fields do not become part of PlayerbotHolder. New registries are translation-unit state in the genuine native lifecycle implementation.

This establishes source-declared layout and vtable compatibility for the exact matching baseline header. It does not replace a native target-compiler layout check or actual active CPP identity check. Any current local body difference must be reviewed before building a replacement object.

The new CPP preserves the baseline snapshot/mutex fix and insert/erase locks, world-thread addclass initialization (#161), pre-login master/account/guild authorization (#934), and selfbot cleanup (#503). Existing methods outside the patch hunks are identical after newline normalization. Additional ordinary-path corrections check masterPlayer before its guild dereference, refuse when no manager exists at the per-class/count gate, and move pending addclass metadata behind successful shared admission.

No integrated header, build plan, protected F509 object, live EXE, or database was changed by this comparison artifact.
