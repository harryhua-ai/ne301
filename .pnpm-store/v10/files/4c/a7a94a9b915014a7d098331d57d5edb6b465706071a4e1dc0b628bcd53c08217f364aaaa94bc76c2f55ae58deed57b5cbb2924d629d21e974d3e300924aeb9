"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.getTranslationsForCatalog = getTranslationsForCatalog;
const getFallbackListForLocale_1 = require("./getFallbackListForLocale");
async function getTranslationsForCatalog(catalog, locale, options) {
    const [catalogs, template] = await Promise.all([
        catalog.readAll(),
        catalog.readTemplate(),
    ]);
    const sourceLocaleCatalog = catalogs[options.sourceLocale] || {};
    const input = Object.assign(Object.assign(Object.assign({}, template), sourceLocaleCatalog), catalogs[locale]);
    return Object.keys(input).reduce((acc, key) => {
        acc[key] = getTranslation(catalogs, input[key], locale, key, options);
        return acc;
    }, {});
}
function sourceLocaleFallback(catalog, key) {
    if (!(catalog === null || catalog === void 0 ? void 0 : catalog[key])) {
        return null;
    }
    return catalog[key].translation || catalog[key].message;
}
function getTranslation(catalogs, msg, locale, key, options) {
    const { fallbackLocales, sourceLocale, onMissing } = options;
    const getTranslation = (_locale) => {
        var _a;
        const localeCatalog = catalogs[_locale];
        return (_a = localeCatalog === null || localeCatalog === void 0 ? void 0 : localeCatalog[key]) === null || _a === void 0 ? void 0 : _a.translation;
    };
    const getMultipleFallbacks = (_locale) => {
        const fL = (0, getFallbackListForLocale_1.getFallbackListForLocale)(fallbackLocales, _locale);
        if (!fL.length)
            return null;
        for (const fallbackLocale of fL) {
            if (catalogs[fallbackLocale] && getTranslation(fallbackLocale)) {
                return getTranslation(fallbackLocale);
            }
        }
    };
    // target locale -> fallback locales -> fallback locales default ->
    // ** (following fallbacks would emit `missing` warning) **
    // -> source locale translation -> source locale message
    // -> template message
    // ** last resort **
    // -> id
    const translation = 
    // Get translation in target locale
    getTranslation(locale) ||
        // We search in fallbackLocales as dependent of each locale
        getMultipleFallbacks(locale) ||
        (sourceLocale &&
            sourceLocale === locale &&
            sourceLocaleFallback(catalogs[sourceLocale], key));
    if (!translation) {
        onMissing === null || onMissing === void 0 ? void 0 : onMissing({
            id: key,
            source: msg.message || sourceLocaleFallback(catalogs[sourceLocale], key),
        });
    }
    return (translation ||
        (sourceLocale && sourceLocaleFallback(catalogs[sourceLocale], key)) ||
        // take from template
        msg.message ||
        key);
}
