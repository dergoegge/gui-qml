// Specification explored during an Antithesis run.
//
// The default QML specification supplies the action set and the properties
// that apply to any Qt Quick application (no QML errors, never stuck, the
// process stays up). This adds the ones specific to this GUI.

import { always } from "@antithesishq/bombadil";
import { extract } from "@antithesishq/bombadil/qml";
import { queryAll, visibleText } from "@antithesishq/bombadil/qml/tree";

export { explore } from "@antithesishq/bombadil/qml/defaults/actions";
export {
  applicationKeepsRunning,
  neverStuck,
  noQmlErrors,
  noSevereMessages,
  pageIsIdentifiable,
} from "@antithesishq/bombadil/qml/defaults/properties";

const MAX_SUPPLY_BTC = 21_000_000;

const texts = extract((state) => visibleText(state));

/** Amounts rendered on screen, in BTC, parsed from their displayed form. */
const displayedAmounts = extract((state) =>
  visibleText(state)
    .map((text) => /(-?[\d,]+(?:[.,]\d+)?)\s*BTC\b/.exec(text))
    .filter((match): match is RegExpExecArray => match !== null)
    .map((match) => Number(match[1].replaceAll(",", "")))
    .filter((amount) => Number.isFinite(amount)),
);

/** Visible popups and dialogs, with a count of the controls inside them. */
const openOverlays = extract((state) =>
  queryAll(
    state.tree,
    (node) =>
      node.visible &&
      (node.type.includes("Popup") ||
        node.type.includes("Dialog") ||
        node.type.includes("Menu")),
  ).map((overlay) => ({
    type: overlay.type,
    objectName: overlay.objectName,
    controls: queryAll(
      overlay,
      (node) => node.clickable && node.visible && node.enabled,
    ).length,
  })),
);

/**
 * Translated strings are always substituted.
 *
 * A `%1` reaching the screen means a `qsTr()` call was rendered without its
 * argument, which is invisible to a test that only checks navigation.
 */
export const noUntranslatedPlaceholders = always(() =>
  texts.current.every((text) => !/%[1-9]/.test(text)),
);

/** No amount on screen exceeds the total supply. */
export const amountsWithinSupply = always(() =>
  displayedAmounts.current.every((amount) => amount <= MAX_SUPPLY_BTC),
);

/**
 * No amount on screen is negative.
 *
 * Balances and transaction amounts are rendered with an explicit sign
 * elsewhere in the UI; a negative number in a BTC-suffixed field is a
 * formatting fault.
 */
export const noNegativeAmounts = always(() =>
  displayedAmounts.current.every((amount) => amount >= 0),
);

/** Every page shows something; a blank page means a load or binding failure. */
export const pagesAreNotBlank = always(() => texts.current.length > 0);

/**
 * An open overlay can always be acted on.
 *
 * A popup with nothing clickable in it traps exploration — and a user — with
 * no way out.
 */
export const overlaysAreDismissable = always(() =>
  openOverlays.current.every((overlay) => overlay.controls > 0),
);
