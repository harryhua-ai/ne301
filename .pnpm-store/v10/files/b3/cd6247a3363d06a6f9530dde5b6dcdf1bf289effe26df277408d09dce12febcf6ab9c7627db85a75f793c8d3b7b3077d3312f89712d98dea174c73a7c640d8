import { LinguiConfigNormalized } from "@lingui/conf";
import { Catalog } from "../catalog";
/**
 * Parse `config.catalogs` and return a list of configured Catalog instances.
 */
export declare function getCatalogs(config: LinguiConfigNormalized): Promise<Catalog[]>;
/**
 * Create catalog for merged messages.
 */
export declare function getCatalogForMerge(config: LinguiConfigNormalized): Promise<Catalog>;
export declare function getCatalogForFile(file: string, catalogs: Catalog[]): {
    locale: string;
    catalog: Catalog;
};
