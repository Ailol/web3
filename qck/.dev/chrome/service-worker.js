const TOGGLE = { type: "qck.nit.toggle" };

chrome.action.onClicked.addListener(async (tab) => {
  if (!tab.id) return;

  try {
    await chrome.tabs.sendMessage(tab.id, TOGGLE);
    return;
  } catch (_) {
    // First click on this page: inject qck.nit, then toggle it.
  }

  try {
    await chrome.scripting.executeScript({
      target: { tabId: tab.id },
      files: ["content.js"]
    });
    await chrome.tabs.sendMessage(tab.id, TOGGLE);
  } catch (error) {
    console.warn("qck.nit cannot run on this page", error);
  }
});
