pref("general.useragent.security",       "U");

pref("security.enable_ssl2",             false);
pref("security.enable_ssl3",             true);
pref("security.enable_tls",		 true);
pref("security.enable_tls_session_tickets", true);

pref("security.ssl.allow_unrestricted_renego_everywhere__temporarily_available_pref", false);
pref("security.ssl.renego_unrestricted_hosts", "");
pref("security.ssl.treat_unsafe_negotiation_as_broken", false);
pref("security.ssl.require_safe_negotiation",  false);

pref("security.ssl2.rc4_128", false);
pref("security.ssl2.rc2_128", false);
pref("security.ssl2.des_ede3_192", false);
pref("security.ssl2.des_64", false);
pref("security.ssl2.rc4_40", false);
pref("security.ssl2.rc2_40", false);
pref("security.ssl3.rsa_rc4_128_md5", true);
pref("security.ssl3.rsa_rc4_128_sha", true);
pref("security.ssl3.rsa_fips_des_ede3_sha", true);
pref("security.ssl3.rsa_des_ede3_sha", true);
pref("security.ssl3.rsa_fips_des_sha", false);
pref("security.ssl3.rsa_des_sha", false);
pref("security.ssl3.rsa_1024_rc4_56_sha", false);
pref("security.ssl3.rsa_1024_des_cbc_sha", false);
pref("security.ssl3.rsa_rc4_40_md5", false);
pref("security.ssl3.rsa_rc2_40_md5", false);
// Camellia is broken on Windows CE for now, see bug 508113
#ifndef WINCE
pref("security.ssl3.dhe_rsa_camellia_256_sha", true);
pref("security.ssl3.dhe_dss_camellia_256_sha", true);
pref("security.ssl3.rsa_camellia_256_sha", true);
pref("security.ssl3.dhe_rsa_camellia_128_sha", true);
pref("security.ssl3.dhe_dss_camellia_128_sha", true);
pref("security.ssl3.rsa_camellia_128_sha", true);
#endif
pref("security.ssl3.dhe_rsa_aes_256_sha", true);
pref("security.ssl3.dhe_dss_aes_256_sha", true);
pref("security.ssl3.rsa_aes_256_sha", true);
pref("security.ssl3.ecdhe_ecdsa_aes_256_sha", true);
pref("security.ssl3.ecdhe_ecdsa_aes_128_sha", true);
pref("security.ssl3.ecdhe_ecdsa_des_ede3_sha", true);
pref("security.ssl3.ecdhe_ecdsa_rc4_128_sha", true);
pref("security.ssl3.ecdhe_ecdsa_null_sha", false);
pref("security.ssl3.ecdhe_rsa_aes_256_sha", true);
pref("security.ssl3.ecdhe_rsa_aes_128_sha", true);
pref("security.ssl3.ecdhe_rsa_des_ede3_sha", true);
pref("security.ssl3.ecdhe_rsa_rc4_128_sha", true);
pref("security.ssl3.ecdhe_rsa_null_sha", false);
pref("security.ssl3.ecdh_ecdsa_aes_256_sha", true);
pref("security.ssl3.ecdh_ecdsa_aes_128_sha", true);
pref("security.ssl3.ecdh_ecdsa_des_ede3_sha", true);
pref("security.ssl3.ecdh_ecdsa_rc4_128_sha", true);
pref("security.ssl3.ecdh_ecdsa_null_sha", false);
pref("security.ssl3.ecdh_rsa_aes_256_sha", true);
pref("security.ssl3.ecdh_rsa_aes_128_sha", true);
pref("security.ssl3.ecdh_rsa_des_ede3_sha", true);
pref("security.ssl3.ecdh_rsa_rc4_128_sha", true);
pref("security.ssl3.ecdh_rsa_null_sha", false);
pref("security.ssl3.dhe_rsa_aes_128_sha", true);
pref("security.ssl3.dhe_dss_aes_128_sha", true);
pref("security.ssl3.rsa_aes_128_sha", true);
pref("security.ssl3.dhe_rsa_des_ede3_sha", true);
pref("security.ssl3.dhe_dss_des_ede3_sha", true);
pref("security.ssl3.dhe_rsa_des_sha", false);
pref("security.ssl3.dhe_dss_des_sha", false);
pref("security.ssl3.rsa_null_sha", false);
pref("security.ssl3.rsa_null_md5", false);
pref("security.ssl3.rsa_seed_sha", true);

pref("security.default_personal_cert",   "Ask Every Time");
pref("security.remember_cert_checkbox_default_setting", true);
pref("security.ask_for_password",        0);
pref("security.password_lifetime",       30);
pref("security.warn_entering_secure",    false);
pref("security.warn_entering_weak",      true);
pref("security.warn_leaving_secure",     false);
pref("security.warn_viewing_mixed",      true);
pref("security.warn_submit_insecure",    false);

pref("security.OCSP.enabled", 1);
pref("security.OCSP.require", false);
