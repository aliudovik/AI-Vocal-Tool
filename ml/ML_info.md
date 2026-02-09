# Machine Learning: Models, Data & Evaluation

This document details the research, data collection, and evaluation process used to build the vocal take ranking system for **AI Vocal Comp**.

The goal of the ML module is to classify vocal segments into quality tiers (e.g., *Emotion/Great* vs. *Bad/Reject*) to automatically suggest the best "comp" to the user.

## 1. Data Collection & Dataset

Data was fully self-collected using a custom web-based tool by real users—ranging from professional vocalists to untrained singers.

### Dataset Variants
Two primary dataset iterations were used during development:

1.  **Unbalanced Dataset (Raw Collection)**
    * **Total samples:** ~329
    * **Distribution:** 117 Emotion, 101 Perfect, 111 Bad.
    * **Characteristics:** Raw inputs, no augmentation.

2.  **Balanced Dataset (Final Training Set)**
    * **Total samples:** ~388
    * **Distribution:** 194 Emotion, 100 Perfect, 94 Bad.
    * **Processing:**
        * Open Vocal Set samples included to increase diversity.
        * Light data augmentation (pitch shifting, noise injection) applied to underrepresented classes to achieve better balance.

## 2. Feature Extraction

We utilized the **OpenSMILE** toolkit to extract acoustic features from the audio segments. Two feature sets were evaluated:

* **ComParE_2016 (6,373 features):** The "Gold Standard" for paralinguistic challenges. Provides a massive, high-dimensional view of the audio signal.
* **eGeMAPS (88 features):** A minimalist feature set designed for affective computing.

**Trade-off:**
While *ComParE_2016* offers higher theoretical accuracy, extracting 6k+ features per segment introduces significant latency (2-3 seconds per take). *eGeMAPS* extraction is near-instant (<100ms), making it viable for the real-time loop workflow.

## 3. Model Configurations & Hyperparameter Sweeps

We evaluated five classifier families: **Random Forest, Balanced Random Forest, Extra Trees, XGBoost, and SVM**.

Testing was conducted in **"Passes"** to systematically isolate the effects of regularization and model capacity.

* **Pass 1 (Baseline):** Standard settings (e.g., `n_estimators=300`, `max_depth=None`).
* **Pass 2 (Conservative):** Stronger regularization to prevent overfitting on the small dataset (`max_depth=10`, `min_samples_leaf=2`, `n_estimators=200`).
* **Pass 3 (Expressive):** High capacity (`n_estimators=500`, `max_depth=None`, `C=5.0`).
* **Pass 4 (Small/Fast):** Lightweight models for speed testing (`n_estimators=100`, `max_depth=8`).
* **Pass Adjusted_2 (Refined):** Based on Pass 2, but with increased depth and estimators to correct underfitting (`n_estimators=250`, `max_depth=20`, `min_samples_leaf=2`).

## 4. Results & Analysis

### A. Impact of Data Balancing
Moving from the Unbalanced to the Balanced dataset yielded the single largest performance jump.

| Dataset | Best Model | ROC AUC | Accuracy | Macro F1 |
| :--- | :--- | :--- | :--- | :--- |
| Unbalanced (Pass 1) | Balanced RF | 0.671 | 0.738 | 0.602 |
| **Balanced (Pass 1)** | **Balanced RF** | **0.815** | **0.690** | **0.690** |

*Analysis:* The unbalanced models struggled to distinguish "Emotion" from "Bad" effectively (AUC ~0.67). Balancing the classes allowed the models to learn the decision boundary properly, jumping to >0.81 AUC immediately.

### B. Hyperparameter Optimization (Balanced Dataset)

Comparing the passes using the *ComParE* feature set:

| Pass Configuration | Best Model | ROC AUC | Note |
| :--- | :--- | :--- | :--- |
| Pass 1 (Baseline) | Balanced RF | 0.815 | Good baseline. |
| Pass 2 (Conservative) | Balanced RF | 0.823 | Reduced overfitting, slight gain. |
| Pass 3 (Expressive) | Random Forest | 0.819 | High variance/overfitting. |
| Pass 4 (Small) | Balanced RF | 0.814 | Surprisingly robust. |
| **Pass Adjusted_2** | **Extra Trees** | **0.835** | **Highest performance.** |

**Why "Adjusted_2" performed best:**
Pass 2 (`max_depth=10`) was too restrictive (underfitting) for the high-dimensional ComParE feature space. By increasing `max_depth` to **20** in *Adjusted_2*, we allowed the trees to capture more complex non-linear patterns without going fully unrestricted (Pass 3), which leads to overfitting. Increasing `n_estimators` to 250 further stabilized the variance.

### C. Feature Set Comparison: ComParE vs. eGeMAPS

We compared the best performing configuration (*Adjusted_2*) across both feature sets.

| Feature Set | Best Model | ROC AUC | Accuracy |
| :--- | :--- | :--- | :--- |
| **ComParE 2016** (6373 feats) | Extra Trees | **0.835** | **0.747** |
| **eGeMAPS** (88 feats) | Balanced RF | 0.784 | 0.667 |

**Analysis:**
1.  **Precision drop:** Reducing features from ~6000 to 88 resulted in a ~0.05 drop in AUC. This confirms that the massive feature space of ComParE captures subtle vocal nuances that eGeMAPS misses.
2.  **Model preference shift:**
    * **Extra Trees** won on high-dimensional data (ComParE). Its random split selection strategy excels in high dimensions by exploring more of the feature space and reducing the "curse of dimensionality" better than standard greedy RF splits.
    * **Balanced Random Forest** won on low-dimensional data (eGeMAPS). With fewer features, the "random" splits of Extra Trees can be too destructive. Balanced RF's standard greedy approach, combined with its robust handling of class imbalance via undersampling, proved superior when features were scarce.

## 5. Final Implementation Choice

For the final **v0.1.0-alpha** release, we prioritized user experience (latency) over raw theoretical accuracy.

* **Selected Model:** Balanced Random Forest
* **Selected Features:** eGeMAPS
* **Configuration:** Pass Adjusted_2 (`n_estimators=250`, `max_depth=20`)

**Reasoning:**
While the ComParE/ExtraTrees model achieved 0.835 AUC, the feature extraction time was prohibitive for a creative "loop" workflow. The eGeMAPS/BalancedRF model (0.784 AUC) offers a valid "good enough" ranking instantly, maintaining the app's promise of speed while still performing significantly better than random chance.# Machine Learning: Models, Data & Evaluation

This document details the research, data collection, and evaluation process used to build the vocal take ranking system for **AI Vocal Comp**.

The goal of the ML module is to classify vocal segments into quality tiers (e.g., *Emotion/Great* vs. *Bad/Reject*) to automatically suggest the best "comp" to the user.

## 1. Data Collection & Dataset

Data was fully self-collected using a custom web-based tool by real users—ranging from professional vocalists to untrained singers.

### Dataset Variants
Two primary dataset iterations were used during development:

1.  **Unbalanced Dataset (Raw Collection)**
    * **Total samples:** ~329
    * **Distribution:** 117 Emotion, 101 Perfect, 111 Bad.
    * **Characteristics:** Raw inputs, no augmentation.

2.  **Balanced Dataset (Final Training Set)**
    * **Total samples:** ~388
    * **Distribution:** 194 Emotion, 100 Perfect, 94 Bad.
    * **Processing:**
        * Open Vocal Set samples included to increase diversity.
        * Light data augmentation (pitch shifting, noise injection) applied to underrepresented classes to achieve better balance.

## 2. Feature Extraction

We utilized the **OpenSMILE** toolkit to extract acoustic features from the audio segments. Two feature sets were evaluated:

* **ComParE_2016 (6,373 features):** The "Gold Standard" for paralinguistic challenges. Provides a massive, high-dimensional view of the audio signal.
* **eGeMAPS (88 features):** A minimalist feature set designed for affective computing.

**Trade-off:**
While *ComParE_2016* offers higher theoretical accuracy, extracting 6k+ features per segment introduces significant latency (2-3 seconds per take). *eGeMAPS* extraction is near-instant (<100ms), making it viable for the real-time loop workflow.

## 3. Model Configurations & Hyperparameter Sweeps

We evaluated five classifier families: **Random Forest, Balanced Random Forest, Extra Trees, XGBoost, and SVM**.

Testing was conducted in **"Passes"** to systematically isolate the effects of regularization and model capacity.

* **Pass 1 (Baseline):** Standard settings (e.g., `n_estimators=300`, `max_depth=None`).
* **Pass 2 (Conservative):** Stronger regularization to prevent overfitting on the small dataset (`max_depth=10`, `min_samples_leaf=2`, `n_estimators=200`).
* **Pass 3 (Expressive):** High capacity (`n_estimators=500`, `max_depth=None`, `C=5.0`).
* **Pass 4 (Small/Fast):** Lightweight models for speed testing (`n_estimators=100`, `max_depth=8`).
* **Pass Adjusted_2 (Refined):** Based on Pass 2, but with increased depth and estimators to correct underfitting (`n_estimators=250`, `max_depth=20`, `min_samples_leaf=2`).

## 4. Results & Analysis

### A. Impact of Data Balancing
Moving from the Unbalanced to the Balanced dataset yielded the single largest performance jump.

| Dataset | Best Model | ROC AUC | Accuracy | Macro F1 |
| :--- | :--- | :--- | :--- | :--- |
| Unbalanced (Pass 1) | Balanced RF | 0.671 | 0.738 | 0.602 |
| **Balanced (Pass 1)** | **Balanced RF** | **0.815** | **0.690** | **0.690** |

*Analysis:* The unbalanced models struggled to distinguish "Emotion" from "Bad" effectively (AUC ~0.67). Balancing the classes allowed the models to learn the decision boundary properly, jumping to >0.81 AUC immediately.

### B. Hyperparameter Optimization (Balanced Dataset)

Comparing the passes using the *ComParE* feature set:

| Pass Configuration | Best Model | ROC AUC | Note |
| :--- | :--- | :--- | :--- |
| Pass 1 (Baseline) | Balanced RF | 0.815 | Good baseline. |
| Pass 2 (Conservative) | Balanced RF | 0.823 | Reduced overfitting, slight gain. |
| Pass 3 (Expressive) | Random Forest | 0.819 | High variance/overfitting. |
| Pass 4 (Small) | Balanced RF | 0.814 | Surprisingly robust. |
| **Pass Adjusted_2** | **Extra Trees** | **0.835** | **Highest performance.** |

**Why "Adjusted_2" performed best:**
Pass 2 (`max_depth=10`) was too restrictive (underfitting) for the high-dimensional ComParE feature space. By increasing `max_depth` to **20** in *Adjusted_2*, we allowed the trees to capture more complex non-linear patterns without going fully unrestricted (Pass 3), which leads to overfitting. Increasing `n_estimators` to 250 further stabilized the variance.

### C. Feature Set Comparison: ComParE vs. eGeMAPS

We compared the best performing configuration (*Adjusted_2*) across both feature sets.

| Feature Set | Best Model | ROC AUC | Accuracy |
| :--- | :--- | :--- | :--- |
| **ComParE 2016** (6373 feats) | Extra Trees | **0.835** | **0.747** |
| **eGeMAPS** (88 feats) | Balanced RF | 0.784 | 0.667 |

**Analysis:**
1.  **Precision drop:** Reducing features from ~6000 to 88 resulted in a ~0.05 drop in AUC. This confirms that the massive feature space of ComParE captures subtle vocal nuances that eGeMAPS misses.
2.  **Model preference shift:**
    * **Extra Trees** won on high-dimensional data (ComParE). Its random split selection strategy excels in high dimensions by exploring more of the feature space and reducing the "curse of dimensionality" better than standard greedy RF splits.
    * **Balanced Random Forest** won on low-dimensional data (eGeMAPS). With fewer features, the "random" splits of Extra Trees can be too destructive. Balanced RF's standard greedy approach, combined with its robust handling of class imbalance via undersampling, proved superior when features were scarce.

## 5. Final Implementation Choice

For the final **v0.1.0-alpha** release, we prioritized user experience (latency) over raw theoretical accuracy.

* **Selected Model:** Balanced Random Forest
* **Selected Features:** eGeMAPS
* **Configuration:** Pass Adjusted_2 (`n_estimators=250`, `max_depth=20`)

**Reasoning:**
While the ComParE/ExtraTrees model achieved 0.835 AUC, the feature extraction time was prohibitive for a creative "loop" workflow. The eGeMAPS/BalancedRF model (0.784 AUC) offers a valid "good enough" ranking instantly, maintaining the app's promise of speed while still performing significantly better than random chance.
