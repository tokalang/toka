# VSCode Extension Publishing Guide

This guide outlines the standard workflow for packaging and publishing the Toka VSCode extension to the Visual Studio Code Marketplace.

---

## 1. Prerequisites

### 1.1. Create a Microsoft Account
You need a personal Microsoft account (e.g., `@outlook.com` or `@hotmail.com`). It is recommended to use a personal account rather than a school or work account to prevent tenant routing issues in Azure DevOps.

### 1.2. Set Up Azure DevOps & Personal Access Token (PAT)
1. Navigate to [Azure DevOps](https://dev.azure.com) and sign in.
2. If prompted, create a new Organization (e.g., `toka-org`). The Organization name does not affect extension publishing.
3. In the top-right corner of the page (next to your profile icon), click the **User Settings** (User + Gear icon) and select **Personal Access Tokens**.
4. Click **New Token**:
   * **Name**: Choose an arbitrary name (e.g., `toka-vscode-token`).
   * **Organization**: **Must select** `All accessible organizations`.
   * **Expiration**: Set to your desired duration (e.g., 30 days, 90 days, or Custom).
   * **Scopes**: Select **Custom defined**, scroll to the bottom, click **Show all scopes**, find **Marketplace**, and check both **Acquire** and **Manage** permissions.
5. Click **Create** and copy the generated token. Keep this token secure; it will not be displayed again.

### 1.3. Register as a Publisher
1. Navigate to the [Visual Studio Marketplace Management Portal](https://marketplace.visualstudio.com/manage).
2. Create a new Publisher:
   * **Name**: The display name shown in the Marketplace (e.g., `Toka`).
   * **ID**: The unique identifier for your publisher profile (e.g., `tokalang`). Only lowercase letters, numbers, and dashes are allowed. This ID will serve as the prefix for your extension identifier (`tokalang.toka-vscode`).
3. Click **Create**.

---

## 2. Local Extension Configuration

1. Locate the extension source folder under `editors/code/`.
2. Inspect `package.json` and ensure the following fields match your Marketplace details:
   * `"publisher"`: Must match your registered **Publisher ID** (e.g., `"tokalang"`).
   * `"name"`: The extension identifier (e.g., `"toka-vscode"`).
   * `"displayName"`: The friendly name shown in the Marketplace (e.g., `"Toka"`). Avoid generic names like `Toka Language Support` to prevent confusion/rejection due to similarities with existing extensions.
   * `"version"`: The SemVer-compliant version number (e.g., `"0.1.2"`).

### Bundle Size Optimization Rule
Ensure package publishing tools like `@vscode/vsce` are declared in `"devDependencies"`, not `"dependencies"`. This prevents `vsce` from packaging thousands of unnecessary package manager files into the final `.vsix` bundle, shrinking the release size from ~90MB down to under 1MB.

---

## 3. Packaging and Local Verification

1. Install the official CLI packaging utility globally:
   ```bash
   npm install -g @vscode/vsce
   ```
2. Navigate to the `editors/code/` directory:
   ```bash
   cd editors/code/
   ```
3. Install the required runtime dependencies:
   ```bash
   npm install
   ```
4. Package the extension into a `.vsix` file:
   ```bash
   vsce package
   ```
   This compiles and packs the source code, generating `toka-vscode-<version>.vsix`. You can test this extension locally in VSCode via **Extensions -> ... (More Actions) -> Install from VSIX...**.

---

## 4. Publishing to the Marketplace

There are two primary methods to release the extension:

### Method A: One-Click CLI Publishing (Recommended)
From the `editors/code/` directory, run:
```bash
vsce publish -p <YOUR_PERSONAL_ACCESS_TOKEN>
```
Replace `<YOUR_PERSONAL_ACCESS_TOKEN>` with the Token generated in Step 1.2. This command compiles, packs, uploads, and releases the extension silently.

### Method B: Manual Web Upload
1. Run `vsce package` locally to obtain the `.vsix` file.
2. Sign in to the [Marketplace Management Portal](https://marketplace.visualstudio.com/manage).
3. Select your Publisher profile.
4. Click **New Extension** -> **Visual Studio Code**.
5. Drag and drop the `.vsix` file into the upload box.

---

## 5. Verification after Publishing
Once uploaded:
1. The Marketplace will take 2-5 minutes to perform automated safety and compatibility scans.
2. Once the scan is successful, the extension will be searchable globally.
3. You can install it directly in VSCode by searching for your Publisher ID:
   ```query
   tokalang
   ```
   Or visit the direct Marketplace link:
   `https://marketplace.visualstudio.com/items?itemName=<PublisherID>.<ExtensionName>`
