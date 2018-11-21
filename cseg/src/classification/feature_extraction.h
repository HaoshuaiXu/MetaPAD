#ifndef __FEATURE_EXTRACTION_H__
#define __FEATURE_EXTRACTION_H__

#include "../utils/utils.h"
#include "../frequent_pattern_mining/frequent_pattern_mining.h"
#include "../data/documents.h"
#include "../model_training/segmentation.h"

using FrequentPatternMining::Pattern;

// === global variables ===
using Documents::totalWordTokens;
using Documents::wordTokens;

using FrequentPatternMining::patterns;
using FrequentPatternMining::pattern2id;
using FrequentPatternMining::id2ends;
// ===

namespace Features
{
// === global variables ===
    struct Hist {
        int timestamp;
        TOTAL_TOKENS_TYPE *cnt;
        int *mark;
        int n;

        Hist(int _n) {
            n = _n;
            mark = new int[n];
            cnt = new TOTAL_TOKENS_TYPE[n];
            timestamp = 0;
        }

        inline int get(int i) {
            assert(0 <= i && i < n);
            if (mark[i] == timestamp) {
                return mark[i];
            } else {
                return 0;
            }
        }

        inline void inc(int i) {
            assert(0 <= i && i < n);
            if (mark[i] != timestamp) {
                mark[i] = timestamp;
                cnt[i] = 1;
            } else {
                ++ cnt[i];
            }
        }

        inline void timeflies() {
            ++ timestamp;
        }
    };
// ===
    inline int getFrequency(const Pattern &pattern) {
        if (pattern2id.count(pattern.hashValue)) {
            return patterns[pattern2id[pattern.hashValue]].currentFreq;
        }
        return 0;
    }

    void extractCompleteness(Pattern &pattern, vector<double> &feature) {
        const vector<TOTAL_TOKENS_TYPE> &tokens = pattern.tokens;
        vector<unordered_set<TOTAL_TOKENS_TYPE>> distinct(tokens.size());
        PATTERN_ID_TYPE id = pattern2id[pattern.hashValue];
        double freq = patterns[id].currentFreq;
        double superFreq = 0, subFreq = freq;

        Pattern subLeft, subRight;
        for (int i = 0; i < pattern.size(); ++ i) {
            if (i) {
                subRight.append(tokens[i]);
            }
            if (i + 1 < pattern.size()) {
                subLeft.append(tokens[i]);
            }
        }
        subFreq = max(subFreq, (double)getFrequency(subLeft));
        subFreq = max(subFreq, (double)getFrequency(subRight));
        feature.push_back(freq / subFreq);

        for (const TOTAL_TOKENS_TYPE &ed : id2ends[id]) {
            TOTAL_TOKENS_TYPE st = ed - tokens.size();
            if (st > 0 && !Documents::isEndOfSentence(st - 1)) {
                Pattern left;
                for (TOTAL_TOKENS_TYPE i = st - 1; i <= ed; ++ i) {
                    left.append(wordTokens[i]);
                }
                superFreq = max(superFreq, (double)getFrequency(left));
            }
            if (!Documents::isEndOfSentence(ed) && ed + 1 < totalWordTokens) {
                Pattern right = pattern;
                right.append(wordTokens[ed + 1]);
                superFreq = max(superFreq, (double)getFrequency(right));
            }
        }
        feature.push_back(superFreq / freq);
    }

    // ready for parallel
    void extractStopwords(const Pattern &pattern, vector<double> &feature) {
        feature.push_back(Documents::stopwords.count(pattern.tokens[0]) || Documents::isDigital[pattern.tokens[0]]);
        feature.push_back(Documents::stopwords.count(pattern.tokens.back())); // || Documents::isDigital[pattern.tokens.back()]);
        double stop = 0, sumIdf = 0;
        int cnt = 0;
        FOR (token, pattern.tokens) {
            stop += Documents::stopwords.count(*token) || Documents::isDigital[*token];
            sumIdf += Documents::idf[*token];
            ++ cnt;
        }
        feature.push_back(stop / pattern.tokens.size());
        feature.push_back(sumIdf / cnt);
    }

    // ready for parallel
    void extractPunctuation(int id, vector<double> &feature) {
        if (id2ends[id].size() == 0) {
            feature.push_back(0);
            feature.push_back(0);
            feature.push_back(0);
            feature.push_back(0);
            return;
        }
        TOTAL_TOKENS_TYPE dash = 0, quote = 0, parenthesis = 0, allCap = 0, allCAP = 0;
        for (const TOTAL_TOKENS_TYPE& ed : id2ends[id]) {
            TOTAL_TOKENS_TYPE st = ed - patterns[id].size() + 1;
            assert(Documents::wordTokens[st] == patterns[id].tokens[0]);

            bool hasDash = false;
            for (int j = st; j < ed && !hasDash; ++ j) {
                hasDash |= Documents::hasDashAfter(j);
            }
            dash += hasDash;

            bool isAllCap = true, isAllCAP = true;
            for (int j = st; j <= ed; ++ j) {
                isAllCap &= Documents::isFirstCapital(j);
                isAllCAP &= Documents::isAllCapital(j);
            }
            allCap += isAllCap;
            allCAP += isAllCAP;
            if (Documents::hasQuoteBefore(st) && Documents::hasQuoteAfter(ed)) {
                ++ quote;
            }
            if (Documents::hasParentThesisBefore(st) && Documents::hasParentThesisAfter(ed)) {
                ++ parenthesis;
            }
        }
        feature.push_back((double)quote / id2ends[id].size());
        feature.push_back((double)dash / id2ends[id].size());
        feature.push_back((double)parenthesis / id2ends[id].size());
        feature.push_back((double)allCap / id2ends[id].size());
        // feature.push_back((double)allCAP / id2ends[id].size()); // not used in SegPhrase
    }

    // ready for parallel
    void extractStatistical(int id, vector<double> &feature) {
        const Pattern &pattern = patterns[id];
        int AB = 0, CD = 0;
        double best = -1;
        for (int i = 0; i + 1 < pattern.size(); ++ i) {
            Pattern left = pattern.substr(0, i + 1);
            Pattern right = pattern.substr(i + 1, pattern.size());

            if (!pattern2id.count(right.hashValue)) {
                cerr << i << " " << pattern.size() << endl;
                left.show();
                right.show();
            }
            assert(pattern2id.count(left.hashValue));
            assert(pattern2id.count(right.hashValue));

            PATTERN_ID_TYPE leftID = pattern2id[left.hashValue], rightID = pattern2id[right.hashValue];

            double current = patterns[leftID].probability * patterns[rightID].probability;
            if (current > best) {
                best = current;
                AB = leftID;
                CD = rightID;
            }
        }

        // prob_feature
        double f1 = pattern.probability / patterns[AB].probability / patterns[CD].probability;
        // occur_feature
        double f2 = patterns[id].currentFreq / sqrt(patterns[AB].currentFreq) / sqrt(patterns[CD].currentFreq);
        // log_occur_feature
        double f3 = sqrt(patterns[id].currentFreq) * log(f1);
        // prob_log_occur
        double f4 = patterns[id].currentFreq * log(f1);
        feature.push_back(f1);
        feature.push_back(f2);
        // feature.push_back(f3); // f3 is ignored in SegPhrase
        feature.push_back(f4);

        const vector<TOKEN_ID_TYPE> &tokens = pattern.tokens;
        unordered_map<TOKEN_ID_TYPE, int> local, context;
        for (int j = 0; j < tokens.size(); ++ j) {
            ++ local[tokens[j]];
        }
        vector<double> outside(pattern.size(), 0);
        double total = 0.0;
        for (TOTAL_TOKENS_TYPE ed : id2ends[id]) {
            TOTAL_TOKENS_TYPE st = ed - patterns[id].size() + 1;
            assert(Documents::wordTokens[st] == patterns[id].tokens[0]);

            for (int sentences = 0; st >= 0 && sentences < 2; -- st) {
                if (Documents::isEndOfSentence(st - 1)) {
                    ++ sentences;
                }
            }
            for (int sentences = 0; ed < Documents::totalWordTokens && sentences < 2; ++ ed) {
                if (Documents::isEndOfSentence(ed)) {
                    ++ sentences;
                }
            }

            assert(Documents::isEndOfSentence(st) && Documents::isEndOfSentence(ed - 1));

            unordered_map<TOKEN_ID_TYPE, int> context;
            for (TOTAL_TOKENS_TYPE j = st + 1; j < ed; ++ j) {
                ++ context[Documents::wordTokens[j]];
            }

            total += 1;
            for (size_t j = 0; j < tokens.size(); ++ j) {
                TOKEN_ID_TYPE diff = context[tokens[j]] - local[tokens[j]];
                assert(diff >= 0);
                outside[j] += diff;
            }
        }
        double sum = 0, norm = 0;
        for (size_t i = 0; i < tokens.size(); ++ i) {
            sum += outside[i] * Documents::idf[tokens[i]];
            norm += Documents::idf[tokens[i]];
        }
        if (total > 0) {
            sum /= total;
        }
        double outsideFeat = sum / norm;
        feature.push_back(outsideFeat);
    }

    int recognize(vector<Pattern> &truth) {
        fprintf(stderr, "Loaded Truth = %d\n", truth.size());
        int truthCnt = 0;
        for (int i = 0; i < truth.size(); ++ i) {
            if (pattern2id.count(truth[i].hashValue)) {
                ++ truthCnt;
                PATTERN_ID_TYPE id = pattern2id[truth[i].hashValue];
                patterns[id].label = truth[i].label;
            }
        }
        fprintf(stderr, "Recognized Truth = %d\n", truthCnt);
        return truthCnt;
    }

    vector<vector<double>> extract(vector<string> &featureNames) {
        // prepare token counts
        const TOTAL_TOKENS_TYPE& corpusTokensN = Documents::totalWordTokens;
        for (PATTERN_ID_TYPE i = 0; i < patterns.size(); ++ i) {
            patterns[i].probability = patterns[i].currentFreq / (corpusTokensN / (double)patterns[i].size());
        }

        featureNames = {"stat_f1", "stat_f2", "stat_f4", "stat_outside",
                        "stopwords_1st", "stopwords_last", "stopwords_ratio", "avg_idf",
                        "punc_quote", "punc_dash", "punc_parenthesis", "first_capitalized",
                        // "all_capitalized",
                        "complete_sub", "complete_super",
                        };

        // compute features for each pattern
        vector<vector<double>> features(patterns.size(), vector<double>());
        # pragma omp parallel for schedule(dynamic, PATTERN_CHUNK_SIZE)
        for (PATTERN_ID_TYPE i = 0; i < patterns.size(); ++ i) {
            if (patterns[i].size() > 1) {
                extractStatistical(i, features[i]);
                extractPunctuation(i, features[i]);
                extractStopwords(patterns[i], features[i]);
                extractCompleteness(patterns[i], features[i]);
                features[i].shrink_to_fit();
            }
        }
        features.shrink_to_fit();
        return features;
    }


    void augment(vector<vector<double>> &features, Segmentation &segmentation, vector<string> &featureNames) {
        features = extract(featureNames);
        return;

        featureNames.push_back("aug_f1");
        featureNames.push_back("aug_f4");
        const TOTAL_TOKENS_TYPE& corpusTokensN = Documents::totalWordTokens;
        # pragma omp parallel for schedule(dynamic, PATTERN_CHUNK_SIZE)
        for (int i = 0; i < patterns.size(); ++ i) {
            if (patterns[i].size() > 1) {
                vector<double> f;
                vector<int> pre;
                double best = segmentation.viterbi_proba_randomPOS(patterns[i].tokens, f, pre);
                double probability = segmentation.getProb(i);

                double f1 = 0, f4 = 0;
                if (patterns[i].currentFreq > 0) {
                    if (best > 1e-9) {
                        f1 = probability / best;
                        f4 = probability * log(f1);
                    } else {
                        f1 = f4 = 1e100;
                    }
                }
                features[i].push_back(f1);
                features[i].push_back(f4);

                features[i].shrink_to_fit();
            }
        }
    }
};

#endif
