set(_enkits_original_base "${FETCHCONTENT_BASE_DIR}")
if(IR_ENKITS_GIT_REF)
    # Git's updater carries local changes across revisions. Separate source,
    # binary and population directories keep the default backport out of custom refs.
    string(SHA256 _enkits_revision_hash "${IR_ENKITS_GIT_REF}")
    set(FETCHCONTENT_BASE_DIR "${_enkits_original_base}/enkits-revisions/${_enkits_revision_hash}")
endif()
