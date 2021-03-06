<?php

/**
 * @file
 * Miscellaneous helper functions.
 */

use Drupal\Core\Link;
use Drupal\Component\Utility\Xss;
use Drupal\Core\Cache\CacheableMetadata;

/**
 * Converts given model descriptions into Drupal form select element #options.
 *
 * @param array $content_models
 *   A list of content models as returned by islandora_get_content_models().
 *
 * @return array
 *   An associative array of content models for use in Drupal form select
 *   element options:
 *   - pid: The PID of the content model
 *     - label: The label to display to the user.
 *
 * @see islandora_get_content_models()
 */
function islandora_basic_collection_get_content_models_as_form_options(array $content_models) {
  $map_content_models_to_options = function ($o) {
    $label = isset($o['label']) ? $o['label'] : $o['name'];
    return format_string('@pid ~ @label', [
      '@pid' => $o['pid'],
      '@label' => $label,
    ]);
  };
  return array_map($map_content_models_to_options, $content_models);
}

/**
 * Lists collections for use in a Drupal form select element #options.
 *
 * Ignores the given collection.
 *
 * @param AbstractObject $object
 *   A collection object to ignore.
 *
 * @return array
 *   An associative array of collections for use in Drupal form select
 *   element options:
 *   - pid: The PID of the collection object.
 *     - label: The label to display to the user.
 *
 * @see islandora_get_collections()
 */
function islandora_basic_collection_get_other_collections_as_form_options(AbstractObject $object) {
  $map_options = function ($o) {
    return Xss::filter($o['label']);
  };
  $options = array_map($map_options, islandora_basic_collection_get_collections());
  // Ignore given collection.
  unset($options[$object->id]);
  return $options;
}

/**
 * Gets a choose namespace element for use in the collection management forms.
 *
 * @param string $default_value
 *   The default value to use for the namespace field.
 *
 * @return array
 *   A Drupal form element for selecting the a namespace.
 */
function islandora_basic_collection_get_namespace_form_element($default_value) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $config = \Drupal::config('islandora.settings');

  $cache_meta = (new CacheableMetadata())
    ->addCacheableDependency($config);

  $restrict_namespace = $config->get('islandora_namespace_restriction_enforced');

  $element = [
    '#type' => $restrict_namespace ? 'select' : 'textfield',
    '#title' => 'Children namespace',
    '#description' => t('Namespace for objects with selected content models.'),
    '#size' => 15,
    '#options' => $restrict_namespace ? array_combine(islandora_get_allowed_namespaces(), islandora_get_allowed_namespaces()) : NULL,
    '#default_value' => $default_value,
  ];

  $cache_meta->applyTo($element);

  return $element;
}

/**
 * A Drupal form select table populated with the given collection's children.
 *
 * @param AbstractObject $object
 *   The collection object.
 * @param array $pager_options
 *   An associative array containing options to pass onto the pager.
 *   - element: The numeric ID of the pager
 *   - fragment: The url fragment to append onto pager links, includes '#'.
 *
 * @return array
 *   A Drupal select table form element.
 */
function islandora_basic_collection_get_children_select_table_form_element(AbstractObject $object, array $pager_options) {
  // Assumes all results are returned although the function description
  // states otherwise.
  $limit = \Drupal::config('islandora_basic_collection.settings')->get('islandora_basic_collection_admin_page_size');
  $page = pager_find_page($pager_options['element']);
  list($count, $results) = islandora_basic_collection_get_member_objects($object, $page, $limit, 'manage');
  $page = pager_default_initialize($count, $limit, $pager_options['element']);
  $rows = [];
  foreach ($results as $result) {
    $pid = $result['object']['value'];
    $label = empty($result['title']['value']) ? t('Untitled') : Xss::filter($result['title']['value']);
    $label = $label . " (" . $pid . ")";
    $owner = empty($result['owner']['value']) ? t('Unowned') : Xss::filter($result['owner']['value']);
    $date_modified = empty($result['date_modified']['value']) ? t('Unknown') : Xss::filter($result['date_modified']['value']);
    $rows[$pid] = [
      'owner' => [
        'data' => [
          '#markup' => '<div>' . $owner . '</div>',
        ],
      ],
      'date_modified' => [
        'data' => [
          '#markup' => '<div>' . $date_modified . '</div>',
        ],
      ],
      'label' => [
        'data' => [
          '#markup' => Link::createFromRoute($label, 'islandora.view_object', ['object' => $pid])->toString(),
        ],
      ],
    ];
  }
  $pager_element = [
    '#type' => 'pager',
    '#quantity' => $limit,
    '#element' => $pager_options['element'],
  ];
  $pager = \Drupal::service('renderer')->render($pager_element);
  $pager = islandora_basic_collection_append_fragment_to_pager_url($pager, $pager_options['fragment']);
  return [
    '#type' => 'tableselect',
    '#header' => [
      'label' => ['data' => t('Label')],
      'owner' => ['data' => t('Owner')],
      'date_modified' => ['data' => t('Date Modified')],
    ],
    '#options' => $rows,
    '#empty' => t('Collection is empty.'),
    '#prefix' => $pager,
    '#suffix' => $pager,
  ];
}

/**
 * Gets a list of all parent pids.
 *
 * @param AbstractObject $object
 *   The object.
 *
 * @return array
 *   The list of parent PIDs.
 */
function islandora_basic_collection_get_parent_pids(AbstractObject $object) {
  $results = array_merge(
      $object->relationships->get(FEDORA_RELS_EXT_URI, 'isMemberOfCollection'),
      $object->relationships->get(FEDORA_RELS_EXT_URI, 'isMemberOf'));
  $map_results = function ($o) {
    return $o['object']['value'];
  };
  $collections = array_map($map_results, $results);
  return array_unique(array_filter($collections));
}

/**
 * Gets a list of all parent pids excluding the given parent.
 *
 * @param AbstractObject $object
 *   The object.
 * @param AbstractObject $parent
 *   The parent of the object.
 *
 * @return array
 *   The list of parent PIDs.
 */
function islandora_basic_collection_get_other_parents(AbstractObject $object, AbstractObject $parent) {
  $parents = islandora_basic_collection_get_parent_pids($object);
  unset($parents[array_search($parent->id, $parents)]);
  return $parents;
}

/**
 * Adds the given object to the given collection.
 *
 * @param AbstractObject $new_member
 *   The object to add.
 * @param AbstractObject $collection
 *   The collection object to add to.
 */
function islandora_basic_collection_add_to_collection(AbstractObject $new_member, AbstractObject $collection) {
  $results = $new_member->relationships->get(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $collection->id);
  if (empty($results)) {
    $new_member->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $collection->id);
  }
}

/**
 * Removes the given object from the given collection.
 *
 * @param AbstractObject $member
 *   The object to remove.
 * @param mixed $collection
 *   The AbstractObject or string PID of the collection to remove object from.
 */
function islandora_basic_collection_remove_from_collection(AbstractObject $member, $collection) {
  if ($collection instanceof AbstractObject) {
    $collection = $collection->id;
  }

  $member->relationships->remove(FEDORA_RELS_EXT_URI, 'isMemberOfCollection', $collection);
  $member->relationships->remove(FEDORA_RELS_EXT_URI, 'isMemberOf', $collection);
}

/**
 * Implements callback_islandora_basic_collection_query_backends().
 */
function islandora_basic_collection_display_query_sparql($collection_object, $page, $limit) {
  list($total, $results) = islandora_basic_collection_get_member_objects($collection_object, $page, $limit);
  $map_to_pids = function ($result) {
    return $result['object']['value'];
  };
  return [$total, array_map($map_to_pids, $results)];
}

/**
 * Helper function to add fragment identifiers to pager URLs.
 *
 * Theme pager doesn't support url fragments in D7 so we insert manually.
 *
 * @param string $pager
 *   The pager markup to be rendered.
 * @param string $fragment
 *   The fragment identifier to be appended.
 *
 * @return string
 *   The pager markup to be rendered with fragment.
 */
function islandora_basic_collection_append_fragment_to_pager_url($pager, $fragment) {
  $pattern = '/href="([^"]+)"/';
  $replace = strtr('href="\1!fragment"', ['!fragment' => $fragment]);
  $pager = preg_replace($pattern, $replace, $pager);
  return $pager;
}
